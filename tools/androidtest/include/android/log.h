enum { ANDROID_LOG_INFO = 4, ANDROID_LOG_ERROR = 6 };
int __android_log_write(int, const char*, const char*);
int __android_log_print(int, const char*, const char*, ...);
