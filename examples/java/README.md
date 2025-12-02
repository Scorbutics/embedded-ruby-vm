# Java Example

Simple Java example demonstrating how to use the embedded Ruby VM from pure Java (no Kotlin runtime).

## Files

- `SimpleJavaExample.java` - Java example demonstrating the Ruby VM API
- `run-java-example.sh` - Helper script to build and run the example

## Quick Start

### Using the Helper Script (Easiest)

```bash
./run-java-example.sh
```

The script automatically:
1. Builds the native library if needed
2. Builds the KMP JAR if needed
3. Extracts Ruby standard library if needed
4. Compiles and runs the example

### Manual Build and Run

#### 1. Build Prerequisites

From the project root:

```bash
# Build native library
./gradlew :ruby-vm-kmp:buildNativeLibsDesktop

# Build KMP JAR
./gradlew :ruby-vm-kmp:desktopJar
```

#### 2. Set Up Runtime Environment

```bash
# Create lib directory and copy native library
mkdir -p ../../lib
cp ../../build/jvm/lib/libembedded-ruby.so ../../lib/  # Linux
# OR
cp ../../build/jvm/lib/libembedded-ruby.dylib ../../lib/  # macOS
```

#### 3. Ensure Ruby Stdlib Exists

```bash
# From project root
unzip core/assets/files/ruby-stdlib.zip -d ruby
```

#### 4. Compile

```bash
javac -cp ../../kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar \
    SimpleJavaExample.java
```

#### 5. Run

```bash
# Linux/Mac
java -cp ../../kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar:. \
    -Djava.library.path=../../lib \
    examples.SimpleJavaExample

# Windows
java -cp ../../kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar;. ^
    -Djava.library.path=../../lib ^
    examples.SimpleJavaExample
```

## What the Example Does

The example demonstrates:

1. **Creating a Log Listener**
   ```java
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
   ```

2. **Creating the Interpreter**
   ```java
   RubyInterpreter interpreter = RubyInterpreter.Companion.create(
       ".",           // Working directory
       "./ruby",      // Ruby stdlib location
       "./lib",       // Native libraries location
       listener
   );
   ```

3. **Creating and Executing Scripts**
   ```java
   RubyScript script = RubyScript.Companion.fromContent(
       "puts 'Hello from Ruby!'\n" +
       "puts 'Ruby version: ' + RUBY_VERSION"
   );

   interpreter.enqueue(script, exitCode -> {
       System.out.println("Script completed: " + exitCode);
       return null;
   });
   ```

4. **Cleanup**
   ```java
   script.destroy();
   interpreter.destroy();
   ```

## Expected Output

```
=== Ruby VM Java Example ===

Creating Ruby interpreter...
Interpreter created!

Executing Ruby script...
[Ruby] Hello from Ruby via Java!
[Ruby] Ruby version: 3.1.0
[Ruby] 2 + 2 = 4
[Ruby] Time: 2024-12-02 14:30:45 +0000

Script completed with exit code: 0

Cleanup complete!
✓ Example finished successfully
```

## Using in Your Project

To use the Ruby VM in your Java project:

### 1. Add Dependency

**Maven:**
```xml
<dependency>
    <groupId>com.scorbutics.rubyvm</groupId>
    <artifactId>ruby-vm-kmp-desktop</artifactId>
    <version>1.0.0-SNAPSHOT</version>
</dependency>
```

**Gradle:**
```kotlin
dependencies {
    implementation("com.scorbutics.rubyvm:ruby-vm-kmp-desktop:1.0.0-SNAPSHOT")
}
```

### 2. Bundle Native Library

Include the native library with your application:
- Linux: `libembedded-ruby.so`
- macOS: `libembedded-ruby.dylib`
- Windows: `embedded-ruby.dll`

Set library path when running:
```bash
java -Djava.library.path=/path/to/libs -jar your-app.jar
```

### 3. Bundle Ruby Stdlib

Include the `ruby/` directory in your application resources.

## Troubleshooting

### UnsatisfiedLinkError

**Problem:** `java.lang.UnsatisfiedLinkError: no embedded-ruby in java.library.path`

**Solution:**
- Verify `-Djava.library.path` points to the directory containing the native library
- Check that the library file exists and has correct permissions
- Ensure the library matches your platform (Linux: `.so`, macOS: `.dylib`, Windows: `.dll`)

### ClassNotFoundException

**Problem:** `ClassNotFoundException: com.scorbutics.rubyvm.RubyInterpreter`

**Solution:**
- Ensure the KMP JAR is in your classpath (`-cp`)
- Build the JAR: `./gradlew :ruby-vm-kmp:desktopJar`

### Script Hangs

**Problem:** Script doesn't produce output or hangs

**Solution:**
- Ensure `./ruby/` directory exists with Ruby standard library
- Check logs for "Ruby stdlib not found" errors
- Extract stdlib: `unzip core/assets/files/ruby-stdlib.zip -d ruby`

## See Also

- **Kotlin Example:** See `examples/kotlin-jvm/` for Kotlin/JVM example
- **Native Example:** See `examples/kotlin-native/` for Kotlin/Native cinterop example
- **API Docs:** See main `README.md` for complete API documentation
