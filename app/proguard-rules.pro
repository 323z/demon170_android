# Keep JNI native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep Demon170 classes
-keep class com.demon170.** { *; }
