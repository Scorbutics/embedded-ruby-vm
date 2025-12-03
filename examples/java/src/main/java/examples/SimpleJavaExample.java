package examples;

import com.scorbutics.rubyvm.*;

/**
 * Simple Java example using the Ruby VM
 * Avoids Kotlin runtime issues in Termux
 */
public class SimpleJavaExample {
    public static void main(String[] args) throws InterruptedException {
        System.out.println("=== Ruby VM Java Example ===\n");

        int[] scriptCount = {0};

        // Create log listener
        LogListener listener = new LogListener() {
            @Override
            public void onLog(String message) {
                System.out.println("[Log Ruby] " + message);
            }

            @Override
            public void onError(String message) {
                System.err.println("[Log Ruby Error] " + message);
            }
        };

        // Create interpreter
        System.out.println("Creating Ruby interpreter...");
        try (RubyInterpreter interpreter = RubyInterpreter.Companion.create(
            ".",
            "./ruby",
            "./lib",
            listener
        )) {
            System.out.println("Interpreter created!\n");

            // Create and execute script
            System.out.println("Executing Ruby script...");
            try (RubyScript script = RubyScript.Companion.fromContent(
                "puts 'Hello from Ruby via Java!'\n" +
                "puts 'Ruby version: ' + RUBY_VERSION\n" +
                "puts '2 + 2 = ' + (2 + 2).to_s\n" +
                "puts 'Time: ' + Time.now.to_s"
            )) {

                interpreter.enqueue(script, exitCode -> {
                    System.out.println("\nScript completed with exit code: " + exitCode);
                    scriptCount[0]++;
                    return null;
                });


                // Wait for script to finish
                while (scriptCount[0] < 1) {
                    Thread.sleep(100);
                }

            }
        }
        System.out.println("\nCleanup complete!");
        System.out.println("✓ Example finished successfully");
    }
}
