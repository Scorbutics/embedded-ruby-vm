# ca_certificates.rb
#
# Loaded from memory by the C runtime right after ruby_options, when
# ca-certificates.c set up a CA bundle (Android) and pointed SSL_CERT_FILE
# to it.
#
# OpenSSL reads SSL_CERT_FILE through ossl_safe_getenv, which ignores the
# environment of a "secure" process (getauxval(AT_SECURE) != 0): Android app
# processes are, through zygote's SELinux domain transition. The bundle is
# added to the stores OpenSSL fills from its default paths instead: Ruby's
# OpenSSL::SSL::SSLContext::DEFAULT_CERT_STORE (Net::HTTP, SSLContext#set_params)
# and any store a script sets up with X509::Store#set_default_paths.

bundle = ENV['SSL_CERT_FILE']
if bundle && File.file?(bundle)
  require 'openssl'

  OpenSSL::X509::Store.prepend(Module.new do
    define_method(:set_default_paths) do
      super()
      add_file(bundle)
    end
  end)
  OpenSSL::SSL::SSLContext::DEFAULT_CERT_STORE.add_file(bundle)
end
