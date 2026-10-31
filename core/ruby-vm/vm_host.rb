# vm_host.rb
#
# Messages between the host application and the Ruby scripts, loaded with
# safe_runner.rb in every VM.
#
#   VMHost.emit('overlay.edited', json)
#     -> the LogListener of the interpreter running this script, as a
#        LOG_STREAM_HOST_EVENT message (the Kotlin binding turns it into a
#        HostEvent for its HostEventListener)
#   VMHost.each_message('overlay.') { |topic, payload| ... }
#     <- what the host posted (its `post` extension runs VMHost.deliver)
#
# A topic is 1 to 128 characters among [A-Za-z0-9_.:-]. Consumers share the
# VM, so each one reads the topics under its own prefix.
module VMHost
  # Must mirror VMHOST_EVENT_TAG / VMHOST_PART_TAG in constants.h
  EVENT_TAG = '<<<VMHOST:EVENT>>> '.freeze
  PART_TAG = '<<<VMHOST:PART>>> '.freeze
  # Longest line of an event. The C side writes its drain markers on the
  # same socket, outside VMLogger's lock; a short write to a local socket
  # goes out in one piece, so no marker lands inside a line.
  LINE_MAX = 512
  TOPIC_PATTERN = /\A[A-Za-z0-9_.:\-]{1,128}\z/.freeze
  # Messages of one topic waiting for their consumer; beyond, VMHost.deliver fails
  MAX_PENDING = 256

  @pending = []
  @mutex = Mutex.new

  class << self
    # Send an event to the host. It arrives after the script's earlier
    # VMLogger lines (puts and print go another way). The payload goes as
    # base64, cut in lines of LINE_MAX at most that the host joins.
    # @param topic [String]
    # @param payload [String] UTF-8 text in practice (JSON...)
    # @return [Boolean] false when no host reads the events: the VM runs
    #   without logging pipes (its vmlogger output is the process's stderr)
    # @raise [ThreadError] outside a thread of an interpreter (a Thread the
    #   script started): the event would have no interpreter to go to
    def emit(topic, payload = '')
      validate_topic(topic)
      interpreter_tag = VMLogger.interpreter_tag
      raise ThreadError, 'VMHost.emit outside a thread of an interpreter' if interpreter_tag.empty?

      io = VMLogger.output_io
      return false if io.nil? || io.fileno == STDERR.fileno

      header = "#{interpreter_tag}#{topic} "
      room = LINE_MAX - EVENT_TAG.bytesize - header.bytesize - 1
      encoded = [payload.to_s].pack('m0')
      chunks = (0...[encoded.size, 1].max).step(room).map { |start| encoded[start, room] }
      lines = chunks.map.with_index do |chunk, index|
        "#{index == chunks.size - 1 ? EVENT_TAG : PART_TAG}#{header}#{chunk}"
      end
      # Not cut short (Timeout, Thread#raise): the host would join the rest
      # of the event to the next one of the same topic. So a script waiting
      # here for a host far behind cannot be interrupted meanwhile.
      Thread.handle_interrupt(Object => :never) { VMLogger.write_output(lines) }
      true
    end

    # Queue a message from the host for #each_message. Run by the host on an
    # interpreter of its own, so it never waits for the scripts reading it.
    # @param topic [String]
    # @param payload [String]
    # @raise [RuntimeError] when MAX_PENDING messages of this topic already
    #   wait: the host's post fails rather than a message being lost
    def deliver(topic, payload)
      validate_topic(topic)
      message = [-topic, payload.dup.force_encoding(Encoding::UTF_8).freeze]
      @mutex.synchronize do
        if @pending.count { |pending_topic, _| pending_topic == topic } >= MAX_PENDING
          raise "VMHost: #{MAX_PENDING} #{topic} messages wait for their consumer, one more refused"
        end

        @pending << message
      end
      nil
    end

    # Take the pending messages whose topic starts with `prefix`, oldest
    # first. Call it from the thread that handles them (a main loop). When
    # the block raises, the messages it did not get yet stay pending.
    # @param prefix [String]
    # @yieldparam topic [String]
    # @yieldparam payload [String]
    # @return [Integer] how many messages the block got
    def each_message(prefix = '')
      raise ArgumentError, 'VMHost.each_message needs a block' unless block_given?

      messages = @mutex.synchronize do
        taken, @pending = @pending.partition { |topic, _| topic.start_with?(prefix) }
        taken
      end
      count = 0
      until messages.empty?
        topic, payload = messages.shift
        count += 1
        yield topic, payload
      end
      count
    ensure
      @mutex.synchronize { @pending.unshift(*messages) } unless messages.nil? || messages.empty?
    end

    private

    def validate_topic(topic)
      raise ArgumentError, "Invalid VMHost topic: #{topic.inspect}" unless topic.is_a?(String) && topic.match?(TOPIC_PATTERN)
    end
  end
end
