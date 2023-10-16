#include <toycc.h>
#include <time.h>
#include <preprocessor.h>

void define_macro(const char *name, const char *buf)
{
	struct Token *tok = tokenize(new_file("<built-in>", 1, buf));
	add_macro(name, true, tok);
}

void undef_macro(const char *name)
{
	struct Macro *m = add_macro(name, true, NULL);
	m->deleted = true;
}

static struct Macro *add_builtin(const char *name, macro_handler_fn *fn)
{
	struct Macro *m = add_macro(name, true, NULL);
	m->handler = fn;
	return m;
}

static struct Token *file_macro(struct Token *tmpl)
{
	while (tmpl->origin)
		tmpl = tmpl->origin;
	return new_str_token(tmpl->file->display_name, tmpl);
}

static struct Token *line_macro(struct Token *tmpl)
{
	while (tmpl->origin)
		tmpl = tmpl->origin;

	int i = tmpl->line_no + tmpl->file->line_delta;
	return new_num_token(i, tmpl);
}

// __COUNTER__ is expanded to serial values starting from 0.
static struct Token *counter_macro(struct Token *tmpl)
{
	static int i = 0;
	return new_num_token(i++, tmpl);
}

// __DATE__ is expanded to the current date, e.g. "May 17 2020".
static const char *format_date(struct tm *tm)
{
	static const char mon[][4] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	};

	return format("\"%s %2d %d\"",
			mon[tm->tm_mon], tm->tm_mday, tm->tm_year + 1900);
}

// __TIME__ is expanded to the current time, e.g. "13:34:03".
static const char *format_time(struct tm *tm)
{
	return format("\"%02d:%02d:%02d\"",
			tm->tm_hour, tm->tm_min, tm->tm_sec);
}

// As "riscv64-linux-gnu-gcc -dM -E - < /dev/null" shown
void init_macros(void)
{
	// Define predefined macros
	define_macro("__riscv", "1");
	define_macro("__SSP_STRONG__", "3");
	define_macro("__DBL_MIN_EXP__", "(-1021)");
	define_macro("__riscv_atomic", "1");
	define_macro("__UINT_LEAST16_MAX__", "0xffff");
	define_macro("__ATOMIC_ACQUIRE", "2");
	define_macro("__FLT128_MAX_10_EXP__", "4932");
	define_macro("__FLT_MIN__", "1.17549435082228750796873653722224568e-38F");
	define_macro("__UINT_LEAST8_TYPE__", "unsigned char");
	// define_macro("__INTMAX_C(c)", "c ## L");
	define_macro("__CHAR_BIT__", "8");
	define_macro("__UINT8_MAX__", "0xff");
	define_macro("__WINT_MAX__", "0xffffffffU");
	define_macro("__riscv_cmodel_medany", "1");
	define_macro("__FLT32_MIN_EXP__", "(-125)");
	define_macro("__ORDER_LITTLE_ENDIAN__", "1234");
	define_macro("__SIZE_MAX__", "0xffffffffffffffffUL");
	define_macro("__WCHAR_MAX__", "0x7fffffff");
	define_macro("__DBL_DENORM_MIN__", "((double)4.94065645841246544176568792868221372e-324L)");
	define_macro("__FLT32X_DECIMAL_DIG__", "17");
	define_macro("__FLT_EVAL_METHOD__", "0");
	define_macro("__FLT64_DECIMAL_DIG__", "17");
	define_macro("__UINT_FAST64_MAX__", "0xffffffffffffffffUL");
	define_macro("__SIG_ATOMIC_TYPE__", "int");
	define_macro("__DBL_MIN_10_EXP__", "(-307)");
	define_macro("__FINITE_MATH_ONLY__", "0");
	define_macro("__FLT32X_MAX_EXP__", "1024");
	define_macro("__GNUC_PATCHLEVEL__", "0");
	define_macro("__FLT32_HAS_DENORM__", "1");
	define_macro("__UINT_FAST8_MAX__", "0xff");
	define_macro("__FLT32_MAX_10_EXP__", "38");
	// define_macro("__INT8_C(c)", "c");
	define_macro("__INT_LEAST8_WIDTH__", "8");
	define_macro("__UINT_LEAST64_MAX__", "0xffffffffffffffffUL");
	define_macro("__SHRT_MAX__", "0x7fff");
	define_macro("__LDBL_MAX__", "1.18973149535723176508575932662800702e+4932L");
	define_macro("__FLT64X_MAX_10_EXP__", "4932");
	define_macro("__LDBL_IS_IEC_60559__", "2");
	define_macro("__FLT64X_HAS_QUIET_NAN__", "1");
	define_macro("__UINT_LEAST8_MAX__", "0xff");
	define_macro("__FLT128_DENORM_MIN__", "6.47517511943802511092443895822764655e-4966F128");
	define_macro("__UINTMAX_TYPE__", "long unsigned int");
	define_macro("__linux", "1");
	define_macro("__FLT_EVAL_METHOD_TS_18661_3__", "0");
	define_macro("__CHAR_UNSIGNED__", "1");
	define_macro("__UINT32_MAX__", "0xffffffffU");
	define_macro("__riscv_fdiv", "1");
	define_macro("__LDBL_MAX_EXP__", "16384");
	define_macro("__FLT128_MIN_EXP__", "(-16381)");
	define_macro("__WINT_MIN__", "0U");
	define_macro("__FLT128_MIN_10_EXP__", "(-4931)");
	define_macro("__FLT32X_IS_IEC_60559__", "2");
	define_macro("__INT_LEAST16_WIDTH__", "16");
	define_macro("__SCHAR_MAX__", "0x7f");
	define_macro("__FLT128_MANT_DIG__", "113");
	define_macro("__WCHAR_MIN__", "(-__WCHAR_MAX__ - 1)");
	// define_macro("__INT64_C(c)", "c ## L");
	define_macro("__SIZEOF_INT__", "4");
	define_macro("__FLT32X_MANT_DIG__", "53");
	define_macro("__FLT64X_EPSILON__", "1.92592994438723585305597794258492732e-34F64x");
	define_macro("__STDC_HOSTED__", "1");
	define_macro("__DBL_DIG__", "15");
	define_macro("__FLT32_DIG__", "6");
	define_macro("__FLT_EPSILON__", "1.19209289550781250000000000000000000e-7F");
	define_macro("__SHRT_WIDTH__", "16");
	define_macro("__FLT32_IS_IEC_60559__", "2");
	define_macro("__LDBL_MIN__", "3.36210314311209350626267781732175260e-4932L");

	// These predefined macros indicates that our u and U
	// chars/strings are UTF-16 and UTF-32 encoded, respectively.
	define_macro("__STDC_UTF_16__", "1");
	define_macro("__STDC_UTF_32__", "1");

	define_macro("__DBL_IS_IEC_60559__", "2");
	define_macro("__FLT64X_DENORM_MIN__", "6.47517511943802511092443895822764655e-4966F64x");
	define_macro("__FP_FAST_FMA", "1");
	define_macro("__FLT32X_HAS_INFINITY__", "1");
	define_macro("__INT32_MAX__", "0x7fffffff");
	define_macro("__unix__", "1");
	define_macro("__INT_WIDTH__", "32");
	define_macro("__SIZEOF_LONG__", "8");
	define_macro("__STDC_IEC_559__", "1");
	define_macro("__STDC_ISO_10646__", "201706L");
	// define_macro("__UINT16_C(c)", "c");
	define_macro("__DECIMAL_DIG__", "36");
	define_macro("__STDC_IEC_559_COMPLEX__", "1");
	define_macro("__FLT64_EPSILON__", "2.22044604925031308084726333618164062e-16F64");
	define_macro("__gnu_linux__", "1");
	define_macro("__FLT128_IS_IEC_60559__", "2");
	define_macro("__FLT64X_MIN_10_EXP__", "(-4931)");
	define_macro("__LDBL_HAS_QUIET_NAN__", "1");
	define_macro("__FLT64_MANT_DIG__", "53");
	define_macro("__FLT64X_MANT_DIG__", "113");
	// define_macro("__GNUC__", "12");
	define_macro("__pie__", "2");
	define_macro("__FLT_HAS_DENORM__", "1");
	define_macro("__SIZEOF_LONG_DOUBLE__", "16");
	define_macro("__BIGGEST_ALIGNMENT__", "16");
	define_macro("__FLT64_MAX_10_EXP__", "308");
	define_macro("__DBL_MAX__", "((double)1.79769313486231570814527423731704357e+308L)");
	define_macro("__riscv_float_abi_double", "1");
	define_macro("__INT_FAST32_MAX__", "0x7fffffffffffffffL");
	define_macro("__DBL_HAS_INFINITY__", "1");
	define_macro("__INTPTR_WIDTH__", "64");
	define_macro("__FLT64X_HAS_INFINITY__", "1");
	define_macro("__UINT_LEAST32_MAX__", "0xffffffffU");
	define_macro("__FLT32X_HAS_DENORM__", "1");
	define_macro("__INT_FAST16_TYPE__", "long int");
	define_macro("__LDBL_HAS_DENORM__", "1");
	define_macro("__FLT128_HAS_INFINITY__", "1");
	define_macro("__DBL_MAX_EXP__", "1024");
	define_macro("__WCHAR_WIDTH__", "32");
	define_macro("__FLT32_MAX__", "3.40282346638528859811704183484516925e+38F32");
	define_macro("__PTRDIFF_MAX__", "0x7fffffffffffffffL");
	define_macro("__FLT32_HAS_QUIET_NAN__", "1");
	define_macro("__LONG_LONG_MAX__", "0x7fffffffffffffffLL");
	define_macro("__SIZEOF_SIZE_T__", "8");
	define_macro("__FLT64X_MIN_EXP__", "(-16381)");
	define_macro("__SIZEOF_WINT_T__", "4");
	define_macro("__LONG_LONG_WIDTH__", "64");
	define_macro("__FLT32_MAX_EXP__", "128");
	define_macro("__GXX_ABI_VERSION", "1017");
	define_macro("__FLT_MIN_EXP__", "(-125)");
	define_macro("__INT16_MAX__", "0x7fff");
	define_macro("__INT_FAST64_TYPE__", "long int");
	define_macro("__FP_FAST_FMAF", "1");
	define_macro("__FLT128_NORM_MAX__", "1.18973149535723176508575932662800702e+4932F128");
	define_macro("__FLT64_DENORM_MIN__", "4.94065645841246544176568792868221372e-324F64");
	define_macro("__DBL_MIN__", "((double)2.22507385850720138309023271733240406e-308L)");
	define_macro("__FLT64X_NORM_MAX__", "1.18973149535723176508575932662800702e+4932F64x");
	define_macro("__SIZEOF_POINTER__", "8");
	define_macro("__LP64__", "1");
	define_macro("__DBL_HAS_QUIET_NAN__", "1");
	define_macro("__FLT32X_EPSILON__", "2.22044604925031308084726333618164062e-16F32x");
	define_macro("__FLT64_MIN_EXP__", "(-1021)");
	define_macro("__FLT64_MIN_10_EXP__", "(-307)");
	define_macro("__riscv_mul", "1");
	define_macro("__FLT64X_DECIMAL_DIG__", "36");
	define_macro("__REGISTER_PREFIX__", "");
	define_macro("__UINT16_MAX__", "0xffff");
	define_macro("__DBL_HAS_DENORM__", "1");
	define_macro("__FLT32_MIN__", "1.17549435082228750796873653722224568e-38F32");
	define_macro("__UINT8_TYPE__", "unsigned char");
	define_macro("__FLT_DIG__", "6");
	define_macro("__NO_INLINE__", "1");
	define_macro("__DEC_EVAL_METHOD__", "2");
	define_macro("__FLT_MANT_DIG__", "24");
	define_macro("__LDBL_DECIMAL_DIG__", "36");
	define_macro("__VERSION__", "\"12.2.0\"");
	// define_macro("__UINT64_C(c)", "c ## UL");
	define_macro("_STDC_PREDEF_H", "1");
	define_macro("__INT_LEAST32_MAX__", "0x7fffffff");
	define_macro("__FLT128_MAX_EXP__", "16384");
	define_macro("__FLT32_MANT_DIG__", "24");
	define_macro("__FLOAT_WORD_ORDER__", "__ORDER_LITTLE_ENDIAN__");
	define_macro("__STDC_IEC_60559_COMPLEX__", "201404L");
	define_macro("__FLT128_HAS_DENORM__", "1");
	define_macro("__FLT128_DIG__", "33");
	define_macro("__SCHAR_WIDTH__", "8");
	// define_macro("__INT32_C(c)", "c");
	define_macro("__ORDER_PDP_ENDIAN__", "3412");
	define_macro("__riscv_muldiv", "1");
	define_macro("__INT_FAST32_TYPE__", "long int");
	define_macro("__UINT_LEAST16_TYPE__", "short unsigned int");
	define_macro("unix", "1");
	define_macro("__SIZE_TYPE__", "long unsigned int");
	define_macro("__UINT64_MAX__", "0xffffffffffffffffUL");
	define_macro("__FLT_IS_IEC_60559__", "2");
	define_macro("__riscv_xlen", "64");
	define_macro("__GNUC_WIDE_EXECUTION_CHARSET_NAME", "\"UTF-32LE\"");
	define_macro("__FLT64X_DIG__", "33");
	define_macro("__INT8_TYPE__", "signed char");
	define_macro("__ELF__", "1");
	define_macro("__FLT_RADIX__", "2");
	define_macro("__INT_LEAST16_TYPE__", "short int");
	define_macro("__LDBL_EPSILON__", "1.92592994438723585305597794258492732e-34L");
	// define_macro("__UINTMAX_C(c)", "c ## UL");
	define_macro("__FLT32X_MIN__", "2.22507385850720138309023271733240406e-308F32x");
	define_macro("__SIG_ATOMIC_MAX__", "0x7fffffff");
	define_macro("__USER_LABEL_PREFIX__", "");
	define_macro("__STDC_IEC_60559_BFP__", "201404L");
	define_macro("__SIZEOF_PTRDIFF_T__", "8");
	define_macro("__riscv_fsqrt", "1");
	define_macro("__LDBL_DIG__", "33");
	define_macro("__FLT64_IS_IEC_60559__", "2");
	define_macro("__FLT32X_MIN_EXP__", "(-1021)");
	define_macro("__INT_FAST16_MAX__", "0x7fffffffffffffffL");
	define_macro("__FLT64_DIG__", "15");
	define_macro("__UINT_FAST32_MAX__", "0xffffffffffffffffUL");
	define_macro("__UINT_LEAST64_TYPE__", "long unsigned int");
	define_macro("__FLT_HAS_QUIET_NAN__", "1");
	define_macro("__FLT_MAX_10_EXP__", "38");
	define_macro("__LONG_MAX__", "0x7fffffffffffffffL");
	define_macro("__FLT64X_HAS_DENORM__", "1");
	define_macro("__FLT_HAS_INFINITY__", "1");
	define_macro("__GNUC_EXECUTION_CHARSET_NAME", "\"UTF-8\"");
	define_macro("__unix", "1");
	define_macro("__UINT_FAST16_TYPE__", "long unsigned int");
	define_macro("__INT_FAST32_WIDTH__", "64");
	define_macro("__CHAR16_TYPE__", "short unsigned int");
	define_macro("__PRAGMA_REDEFINE_EXTNAME", "1");
	define_macro("__SIZE_WIDTH__", "64");
	define_macro("__INT_LEAST16_MAX__", "0x7fff");
	define_macro("__INT64_MAX__", "0x7fffffffffffffffL");
	define_macro("__FLT32_DENORM_MIN__", "1.40129846432481707092372958328991613e-45F32");
	define_macro("__SIG_ATOMIC_WIDTH__", "32");
	define_macro("__INT_LEAST64_TYPE__", "long int");
	define_macro("__INT16_TYPE__", "short int");
	define_macro("__INT_LEAST8_TYPE__", "signed char");
	define_macro("__STDC_VERSION__", "201710L");
	define_macro("__INT_FAST8_MAX__", "0x7f");
	define_macro("__FLT128_MAX__", "1.18973149535723176508575932662800702e+4932F128");
	define_macro("__INTPTR_MAX__", "0x7fffffffffffffffL");
	define_macro("linux", "1");
	define_macro("__FLT64_HAS_QUIET_NAN__", "1");
	define_macro("__FLT32_MIN_10_EXP__", "(-37)");
	define_macro("__FLT32X_DIG__", "15");
	define_macro("__PTRDIFF_WIDTH__", "64");
	define_macro("__LDBL_MANT_DIG__", "113");
	define_macro("__riscv_m", "2000000");
	define_macro("__FLT64_HAS_INFINITY__", "1");
	define_macro("__FLT64X_MAX__", "1.18973149535723176508575932662800702e+4932F64x");
	define_macro("__SIG_ATOMIC_MIN__", "(-__SIG_ATOMIC_MAX__ - 1)");
	define_macro("__INTPTR_TYPE__", "long int");
	define_macro("__UINT16_TYPE__", "short unsigned int");
	define_macro("__WCHAR_TYPE__", "int");
	define_macro("__SIZEOF_FLOAT__", "4");
	define_macro("__pic__", "2");
	define_macro("__UINTPTR_MAX__", "0xffffffffffffffffUL");
	define_macro("__INT_FAST64_WIDTH__", "64");
	define_macro("__FLT32_DECIMAL_DIG__", "9");
	define_macro("__INT_FAST64_MAX__", "0x7fffffffffffffffL");
	define_macro("__FLT_NORM_MAX__", "3.40282346638528859811704183484516925e+38F");
	define_macro("__FLT32_HAS_INFINITY__", "1");
	define_macro("__FLT64X_MAX_EXP__", "16384");
	define_macro("__UINT_FAST64_TYPE__", "long unsigned int");
	define_macro("__riscv_a", "2001000");
	define_macro("__riscv_c", "2000000");
	define_macro("__riscv_d", "2002000");
	define_macro("__riscv_f", "2002000");
	define_macro("__riscv_i", "2001000");
	define_macro("__INT_MAX__", "0x7fffffff");
	define_macro("__riscv_zicsr", "2000000");
	define_macro("__INT64_TYPE__", "long int");
	define_macro("__FLT_MAX_EXP__", "128");
	define_macro("__ORDER_BIG_ENDIAN__", "4321");
	define_macro("__DBL_MANT_DIG__", "53");
	define_macro("__INT_LEAST64_MAX__", "0x7fffffffffffffffL");
	define_macro("__FP_FAST_FMAF32", "1");
	define_macro("__WINT_TYPE__", "unsigned int");
	define_macro("__UINT_LEAST32_TYPE__", "unsigned int");
	define_macro("__SIZEOF_SHORT__", "2");
	define_macro("__FLT32_NORM_MAX__", "3.40282346638528859811704183484516925e+38F32");
	define_macro("__LDBL_MIN_EXP__", "(-16381)");
	define_macro("__riscv_compressed", "1");
	define_macro("__FLT64_MAX__", "1.79769313486231570814527423731704357e+308F64");
	define_macro("__WINT_WIDTH__", "32");
	define_macro("__FP_FAST_FMAF64", "1");
	define_macro("__INT_LEAST8_MAX__", "0x7f");
	define_macro("__INT_LEAST64_WIDTH__", "64");
	define_macro("__FLT32X_MAX_10_EXP__", "308");
	define_macro("__SIZEOF_INT128__", "16");
	define_macro("__FLT64X_IS_IEC_60559__", "2");
	define_macro("__LDBL_MAX_10_EXP__", "4932");
	define_macro("__ATOMIC_RELAXED", "0");
	define_macro("__DBL_EPSILON__", "((double)2.22044604925031308084726333618164062e-16L)");
	define_macro("__FLT128_MIN__", "3.36210314311209350626267781732175260e-4932F128");
	define_macro("_LP64", "1");
	define_macro("__UINT8_C(c)", "c");
	define_macro("__FLT64_MAX_EXP__", "1024");
	define_macro("__INT_LEAST32_TYPE__", "int");
	define_macro("__SIZEOF_WCHAR_T__", "4");
	define_macro("__UINT64_TYPE__", "long unsigned int");
	define_macro("__FLT64_NORM_MAX__", "1.79769313486231570814527423731704357e+308F64");
	define_macro("__FLT128_HAS_QUIET_NAN__", "1");
	define_macro("__INTMAX_MAX__", "0x7fffffffffffffffL");
	define_macro("__INT_FAST8_TYPE__", "signed char");
	define_macro("__FLT64X_MIN__", "3.36210314311209350626267781732175260e-4932F64x");
	define_macro("__LDBL_HAS_INFINITY__", "1");
	define_macro("__GNUC_STDC_INLINE__", "1");
	define_macro("__FLT64_HAS_DENORM__", "1");
	define_macro("__FLT32_EPSILON__", "1.19209289550781250000000000000000000e-7F32");
	define_macro("__FP_FAST_FMAF32x", "1");
	define_macro("__DBL_DECIMAL_DIG__", "17");
	define_macro("__INT_FAST8_WIDTH__", "8");
	define_macro("__FLT32X_MAX__", "1.79769313486231570814527423731704357e+308F32x");
	define_macro("__DBL_NORM_MAX__", "((double)1.79769313486231570814527423731704357e+308L)");
	define_macro("__BYTE_ORDER__", "__ORDER_LITTLE_ENDIAN__");
	define_macro("__INTMAX_WIDTH__", "64");
	define_macro("__riscv_flen", "64");
	// define_macro("__UINT32_C(c)", "c ## U");
	define_macro("__riscv_arch_test", "1");
	define_macro("__FLT_DENORM_MIN__", "1.40129846432481707092372958328991613e-45F");
	define_macro("__INT8_MAX__", "0x7f");
	define_macro("__LONG_WIDTH__", "64");
	define_macro("__PIC__", "2");
	define_macro("__UINT_FAST32_TYPE__", "long unsigned int");
	define_macro("__FLT32X_NORM_MAX__", "1.79769313486231570814527423731704357e+308F32x");
	define_macro("__CHAR32_TYPE__", "unsigned int");
	define_macro("__FLT_MAX__", "3.40282346638528859811704183484516925e+38F");
	define_macro("__INT32_TYPE__", "int");
	define_macro("__SIZEOF_DOUBLE__", "8");
	define_macro("__FLT_MIN_10_EXP__", "(-37)");
	define_macro("__FLT64_MIN__", "2.22507385850720138309023271733240406e-308F64");
	define_macro("__INT_LEAST32_WIDTH__", "32");
	define_macro("__INTMAX_TYPE__", "long int");
	define_macro("__FLT32X_HAS_QUIET_NAN__", "1");
	define_macro("__ATOMIC_CONSUME", "1");
	// define_macro("__GNUC_MINOR__", "2");
	define_macro("__INT_FAST16_WIDTH__", "64");
	define_macro("__UINTMAX_MAX__", "0xffffffffffffffffUL");
	define_macro("__PIE__", "2");
	define_macro("__FLT32X_DENORM_MIN__", "4.94065645841246544176568792868221372e-324F32x");
	define_macro("__DBL_MAX_10_EXP__", "308");
	define_macro("__LDBL_DENORM_MIN__", "6.47517511943802511092443895822764655e-4966L");
	// define_macro("__INT16_C(c)", "c");
	define_macro("__STDC__", "1");
	define_macro("__PTRDIFF_TYPE__", "long int");
	define_macro("__riscv_div", "1");
	define_macro("__ATOMIC_SEQ_CST", "5");
	define_macro("__riscv_zifencei", "2000000");
	define_macro("__UINT32_TYPE__", "unsigned int");
	define_macro("__FLT32X_MIN_10_EXP__", "(-307)");
	define_macro("__UINTPTR_TYPE__", "long unsigned int");
	define_macro("__linux__", "1");
	define_macro("__LDBL_MIN_10_EXP__", "(-4931)");
	define_macro("__FLT128_EPSILON__", "1.92592994438723585305597794258492732e-34F128");
	define_macro("__SIZEOF_LONG_LONG__", "8");
	define_macro("__FLT128_DECIMAL_DIG__", "36");
	define_macro("__riscv_cmodel_pic", "1");
	define_macro("__FLT_DECIMAL_DIG__", "9");
	define_macro("__UINT_FAST16_MAX__", "0xffffffffffffffffUL");
	define_macro("__LDBL_NORM_MAX__", "1.18973149535723176508575932662800702e+4932L");
	define_macro("__UINT_FAST8_TYPE__", "unsigned char");
	define_macro("__ATOMIC_ACQ_REL", "4");
	define_macro("__ATOMIC_RELEASE", "3");

	add_builtin("__FILE__", file_macro);
	add_builtin("__LINE__", line_macro);

	add_builtin("__COUNTER__", counter_macro);

	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	define_macro("__DATE__", format_date(tm));
	define_macro("__TIME__", format_time(tm));
}
