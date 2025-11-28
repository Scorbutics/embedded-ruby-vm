package examples;

import com.scorbutics.rubyvm.*;

/**
 * Simple Java example using the Ruby VM
 * Avoids Kotlin runtime issues in Termux
 */
public class SimpleJavaExample {
    public static void main(String[] args) throws InterruptedException {
        System.out.println("=== Ruby VM Java Example ===\n");

        // Create log listener
        LogListener listener = new LogListener() {
            @Override
            public void onLog(String message) {
                System.out.println("[Ruby] " + message);
            }

            @Override
            public void onError(String message) {
                System.err.println("[Ruby Error] " + message);
            }
        };

        // Create interpreter
        System.out.println("Creating Ruby interpreter...");
        RubyInterpreter interpreter = RubyInterpreter.Companion.create(
            ".",
            "./ruby",
            "./lib",
            listener
        );
        System.out.println("Interpreter created!\n");

        // Create and execute script
        System.out.println("Executing Ruby script...");
        RubyScript script = RubyScript.Companion.fromContent(
            "puts 'Hello from Ruby via Java!'\n" +
            "puts 'Ruby version: ' + RUBY_VERSION\n" +
            "puts '2 + 2 = ' + (2 + 2).to_s\n" +
            "puts 'Time: ' + Time.now.to_s"
        );

        interpreter.enqueue(script, exitCode -> {
            System.out.println("\nScript completed with exit code: " + exitCode);
            return null;
        });

        // Wait for async execution
        Thread.sleep(30000);

        // Cleanup
        script.destroy();
        interpreter.destroy();
        System.out.println("\nCleanup complete!");
        System.out.println("✓ Example finished successfully");
    }
}
