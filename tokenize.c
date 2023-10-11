#include <toycc.h>
#include <type.h>

// input file
static struct File *current_file;

// A list of all input files
static struct File **input_files;

// True if the current position follows a space character
static bool has_space;

void __attribute__((noreturn))
error_at(const char *loc, const char *fmt, ...)
{
	// get a line number
	int line_no = 1;

	for (const char *p = current_file->contents; p < loc; p++)
		if (*p == '\n')
			line_no++;
	va_list ap;
	va_start(ap, fmt);
	verror_at(current_file->name, current_file->contents,
		  line_no, loc, fmt, ap);
	va_end(ap);
	exit(1);
}

bool consume(struct Token **rest, struct Token *tok, const char *str)
{
	if (equal(tok, str)) {
		*rest = tok->next;
		return true;
	}

	*rest = tok;
	return false;
}

// True if the current position is at the beginning of a line
static bool at_bol;

static struct Token *new_token(enum TokenKind kind,
				const char *start, const char *end)
{
	struct Token *tok = calloc(1, sizeof(struct Token));

	tok->kind = kind;
	tok->loc = start;
	tok->len = end - start;
	tok->file = current_file;
	tok->at_bol = at_bol;
	tok->has_space = has_space;

	at_bol = false;
	has_space = false;

	return tok;
}

static bool startwith(const char *p, const char *q)
{
	return strncmp(p, q, strlen(q)) == 0;
}

// punctuator
static int read_punct(const char *p)
{
	static const char * const kw[] = {
		"<<=",
		">>=",
		"...",
		"==",
		"!=",
		"<=",
		">=",
		"->",
		"+=",
		"-=",
		"*=",
		"/=",
		"++",
		"--",
		"%=",
		"&=",
		"|=",
		"^=",
		"&&",
		"||",
		"<<",
		">>",
		"##",
	};

	for (size_t i = 0; i < ARRAY_SIZE(kw); i++)
		if (startwith(p, kw[i]))
			return strlen(kw[i]);

	return ispunct(*p) ? 1 : 0;
}

// Read an identifier and returns the length of it.
// If p does not point to a valid identifier, 0 is returned.
static int read_ident(const char *start)
{
	const char *p = start;
	uint32_t c = decode_utf8(&p, p);
	if (!is_ident1(c))
		return 0;

	for (;;) {
		const char *q;
		c = decode_utf8(&q, p);
		if (!is_ident2(c))
			return p - start;
		p = q;
	}
}

static bool is_keyword(struct Token *tok)
{
	static const char * const kw[] = {
		"return",
		"if",
		"else",
		"for",
		"while",
		"short",
		"int",
		"long",
		"sizeof",
		"char",
		"struct",
		"union",
		"void",
		"typedef",
		"_Bool",
		"enum",
		"static",
		"goto",
		"break",
		"continue",
		"switch",
		"case",
		"default",
		"extern",
		"_Alignof",
		"_Alignas",
		"do",
		"signed",
		"unsigned",
		"const",
		"volatile",
		"auto",
		"register",
		"restrict",
		"__restrict",
		"__restrict__",
		"_Noreturn",
		"float",
		"double",
	};

	for (size_t i = 0; i < ARRAY_SIZE(kw); i++)
		if (equal(tok, kw[i]))
			return true;
	return false;
}

static bool isodigit(char c)
{
	return '0' <= c && c <= '7';
}

static int from_oct(char c)
{
	return c - '0';
}

static int from_hex(char c)
{
	if ('0' <= c && c <= '9')
		return c - '0';
	if ('a' <= c && c <= 'f')
		return c - 'a' + 10;
	return c - 'A' + 10;
}

static int read_escaped_char(const char **new_pos, const char *p)
{
	if (isodigit(*p)) {
		// read an octal number
		int c = 0;
		// '\xxx'
		for (int i = 0; isodigit(*p) && i < 3; i++, p++)
			c = (c << 3) + from_oct(*p);
		*new_pos = p;
		return c;
	}

	if (*p == 'x') {
		// read a hexadecimal number
		p++;
		if (!isxdigit(*p))
			error_at(p, "invalid hex escape sequence");

		int c = 0;
		for (; isxdigit(*p); p++)
			c = (c << 4) + from_hex(*p);
		*new_pos = p;
		return c;
	}

	*new_pos = p + 1;
	switch (*p) {
	case 'a':
		return 7;
	case 'b':
		return 8;
	case 't':
		return 9;
	case 'n':
		return 10;
	case 'v':
		return 11;
	case 'f':
		return 12;
	case 'r':
		return 13;
	// [GNU] \e for the ASCII escape character is a GNU C extension.
	case 'e':
		return 27;
	default:
		return *p;
	}
}

// Find a closing double-quote.
static const char *string_literal_end(const char *start)
{
	const char *p = start;

	for (; *p != '"'; p++) {
		if (*p == '\n' || *p == '\0')
			// continue;
			error_at(start, "unclosed string literal");

		if (*p == '\\')
			// skip next char
			p++;
	}
	return p;
}

// utf-8 => wide-char string
static struct Token *read_string_literal(const char *start,
					const char *quote)
{
	const char *end = string_literal_end(quote + 1);
	char *buf = malloc(end - quote);
	int len = 0;

	// skip '"'
	for (const char *p = quote + 1; p < end;) {
		if (*p == '\\')
			buf[len++] = read_escaped_char(&p, p + 1);
		else
			buf[len++] = *p++;
	}
	// terminated with '\0'
	buf[len++] = '\0';

	struct Token *tok = new_token(TK_STR, start, end + 1);
	tok->ty = array_of(p_ty_char(), len);
	tok->str = buf;
	return tok;
}

// Read a UTF-8-encoded string literal and transcode it in UTF-16.
//
// UTF-16 is yet another variable-width encoding for Unicode. Code
// points smaller than U+10000 are encoded in 2 bytes. Code points
// equal to or larger than that are encoded in 4 bytes. Each 2 bytes
// in the 4 byte sequence is called "surrogate", and a 4 byte sequence
// is called a "surrogate pair".
//
// utf-16 => wide-char string
static struct Token *read_utf16_string_literal(const char *start,
						const char *quote)
{
	const char *end = string_literal_end(quote + 1);
	// `sizeof(uint32_t)` is reserved for 4-byte wide char
	uint16_t *buf = malloc(sizeof(uint32_t) * (end - quote));
	int len = 0;

	for (const char *p = quote + 1; p < end;) {
		if (*p == '\\') {
			buf[len++] = read_escaped_char(&p, p + 1);
			continue;
		}

		uint32_t c = decode_utf8(&p, p);
		if (c < 0x10000) {
			// Encode a code point in 2 bytes.
			buf[len++] = c;
		} else {
			// Encode a code point in 4 bytes.
			c -= 0x10000;
			// Leading Code Unit
			buf[len++] = 0xd800 + ((c >> 10) & 0x3ff);
			// Trailing Code Unit
			buf[len++] = 0xdc00 + (c & 0x3ff);
		}
	}
	buf[len++] = 0;

	struct Token *tok = new_token(TK_STR, start, end + 1);
	tok->ty = array_of(p_ty_ushort(), len);
	tok->str = (char *)buf;
	return tok;
}

// Read a UTF-8-encoded string literal and transcode it in UTF-32.
//
// UTF-32 is a fixed-width encoding for Unicode. Each code point is
// encoded in 4 bytes.
//
// utf-32 => wide-char string
static struct Token *read_utf32_string_literal(const char *start,
						const char *quote,
						struct Type *ty)
{
	const char *end = string_literal_end(quote + 1);
	uint32_t *buf = malloc(sizeof(uint32_t) * (end - quote));
	int len = 0;

	for (const char *p = quote + 1; p < end;) {
		if (*p == '\\')
			buf[len++] = read_escaped_char(&p, p + 1);
		else
			buf[len++] = decode_utf8(&p, p);
	}
	buf[len++] = 0;

	struct Token *tok = new_token(TK_STR, start, end + 1);
	tok->ty = array_of(ty, len);
	tok->str = (char *)buf;
	return tok;
}

struct Token *tokenize_string_literal(struct Token *tok, struct Type *basety)
{
	struct Token *t;

	if (basety->size == 2)
		t = read_utf16_string_literal(tok->loc, tok->loc);
	else
		t = read_utf32_string_literal(tok->loc, tok->loc, basety);

	t->next = tok->next;
	return t;
}

// initialize line info for all tokens
static void add_line_number(struct Token *tok)
{
	const char *p = current_file->contents;
	int n = 1;

	do {
		if (p == tok->loc) {
			tok->line_no = n;
			tok = tok->next;
		}

		if (*p == '\n')
			n++;
	} while (*p++);
}

static struct Token *read_char_literal(const char *start,
					const char *quote,
					struct Type *ty)
{
	const char *p = quote + 1;
	if (*p == '\0')
		error_at(start, "unclosed char literal");

	int c;
	if (*p == '\\')
		c = read_escaped_char(&p, p + 1);
	else
		c = decode_utf8(&p, p);

	const char *end = strchr(p, '\'');
	if (!end)
		error_at(p, "unclosed char literal");

	// skip '\''
	struct Token *tok = new_token(TK_NUM, start, end + 1);
	tok->val = c;
	tok->ty = ty;
	return tok;
}

static bool convert_pp_int(struct Token *tok)
{
	const char *p = tok->loc;

	// Read a binary, octal, decimal or hexadecimal number.
	int base = 10;
	// isalnum: is alpha or number
	if (!strncasecmp(p, "0x", 2) && isxdigit(p[2])) {
		p += 2;
		base = 16;
	} else if (!strncasecmp(p, "0b", 2) &&
		  (p[2] == '0' || p[2] == '1')) {
		p += 2;
		base = 2;
	} else if (*p == '0') {
		base = 8;
	}

	int64_t val = strtoul(p, (char **)&p, base);

	// read U, L or LL suffixes
	bool l = false;
	bool u = false;

	if (startwith(p, "LLU") || startwith(p, "LLu") ||
	    startwith(p, "llU") || startwith(p, "llu") ||
	    startwith(p, "ULL") || startwith(p, "Ull") ||
	    startwith(p, "uLL") || startwith(p, "ull")) {
		p += 3;
		l = u = true;

	} else if (!strncasecmp(p, "lu", 2) || !strncasecmp(p, "ul", 2)) {
		p += 2;
		l = u = true;

	} else if (startwith(p, "LL") || startwith(p, "ll")) {
		p += 2;
		l = true;

	} else if (*p == 'L' || *p == 'l') {
		p++;
		l = true;

	} else if (*p == 'U' || *p == 'u') {
		p++;
		u = true;
	}

	if (p != tok->loc + tok->len)
		return false;

	// infer a type
	struct Type *ty;
	if (base == 10) {
		if (l && u)
			ty = p_ty_ulong();
		else if (l)
			ty = p_ty_long();
		else if (u)
			ty = (val >> 32) ? p_ty_ulong() : p_ty_uint();
		else
			ty = (val >> 31) ? p_ty_long() : p_ty_int();
	} else {
		if (l && u)
			ty = p_ty_ulong();
		else if (l)
			ty = (val >> 63) ? p_ty_ulong() : p_ty_long();
		else if (u)
			ty = (val >> 32) ? p_ty_ulong() : p_ty_uint();
		else if (val >> 63)
			ty = p_ty_ulong();
		else if (val >> 32)
			ty = p_ty_long();
		else if (val >> 31)
			ty = p_ty_uint();
		else
			ty = p_ty_int();
	}

	tok->kind = TK_NUM;
	tok->val = val;
	tok->ty = ty;
	return true;
}

// The definition of the numeric literal at the preprocessing stage
// is more relaxed than the definition of that at the later stages.
// In order to handle that, a numeric literal is tokenized as a
// "pp-number" token first and then converted to a regular number
// token after preprocessing.
//
// This function converts a pp-number token to a regular number token.
static void convert_pp_number(struct Token *tok)
{
	// Try to parse as an integer constant
	if (convert_pp_int(tok))
		return;

	// If it's not an integer, it must be a floating point constant
	char *end;
	double val = strtod(tok->loc, &end);

	struct Type *ty;
	if (*end == 'f' || *end == 'F') {
		ty = p_ty_float();
		end++;

	} else if (*end == 'l' || *end == 'L') {
		ty = p_ty_double();
		end++;

	} else {
		// implicit in default
		ty = p_ty_double();
	}

	if (tok->loc + tok->len != end)
		error_tok(tok, "invalid numeric constant");

	tok->kind = TK_NUM;
	tok->fval = val;
	tok->ty = ty;
}

void convert_pp_tokens(struct Token *tok)
{
	for (struct Token *t = tok; t->kind != TK_EOF; t = t->next) {
		if (is_keyword(t))
			t->kind = TK_KEYWORD;
		else if (t->kind == TK_PP_NUM)
			convert_pp_number(t);
	}
}

// Tokenize a given string and returns new tokens.
struct Token *tokenize(struct File *file)
{
	const char *p = file->contents;
	struct Token head;
	struct Token *cur = &head;

	current_file = file;
	at_bol = true;
	has_space = false;

	while (*p) {
		// Skip line comments
		if (startwith(p, "//")) {
			p += 2;
			while (*p != '\n')
				p++;

			has_space = true;
			continue;
		}

		// Skip block comments
		if (startwith(p, "/*")) {
			const char *q = strstr(p + 2, "*/");
			if (!q)
				error_at(p, "unclosed block comment");
			p = q + 2;
			has_space = true;
			continue;
		}

		// skip newline
		if (*p == '\n') {
			p++;
			at_bol = true;
			has_space = false;
			continue;
		}

		// Skip whitespace characters
		if (isspace(*p)) {
			p++;
			has_space = true;
			continue;
		}

		// Numeric literal
		if (isdigit(*p) || (*p == '.' && isdigit(p[1]))) {
			const char *q = p++;
			for (;;) {
				if (p[0] && p[1] &&
					strchr("eEpP", p[0]) &&
					strchr("+-", p[1]))
					p += 2;
				else if (isalnum(*p) || *p == '.')
					p++;
				else
					break;
			}
			cur = cur->next = new_token(TK_PP_NUM, q, p);
			continue;
		}

		// string literal
		if (*p == '"') {
			cur->next = read_string_literal(p, p);
			cur = cur->next;
			p += cur->len;
			continue;
		}

		// UTF-8 string literal
		if (startwith(p, "u8\"")) {
			cur = cur->next = read_string_literal(p, p + 2);
			p += cur->len;
			continue;
		}

		// UTF-16 string literal
		if (startwith(p, "u\"")) {
			cur = cur->next = read_utf16_string_literal(p, p + 1);
			p += cur->len;
			continue;
		}

		// UTF-32 string literal
		if (startwith(p, "U\"")) {
			cur = cur->next = read_utf32_string_literal(p, p + 1, p_ty_uint());
			p += cur->len;
			continue;
		}

		// Wide string literal
		if (startwith(p, "L\"")) {
			cur = cur->next = read_utf32_string_literal(p, p + 1, p_ty_int());
			p += cur->len;
			continue;
		}

		// character literal
		if (*p == '\'') {
			cur->next = read_char_literal(p, p, p_ty_int());
			cur = cur->next;
			// convert to single char
			cur->val = (char)cur->val;
			p += cur->len;
			continue;
		}

		// Wide character literal
		if (startwith(p, "L'")) {
			cur = cur->next = read_char_literal(p, p + 1, p_ty_int());
			p += cur->len;
			continue;
		}

		// UTF-16 character literal
		// utf-16 => wide character
		if (startwith(p, "u'")) {
			cur = cur->next = read_char_literal(p, p + 1, p_ty_ushort());
			cur->val = (uint16_t)cur->val;
			p += cur->len;
			continue;
		}

		// UTF-32 character literal
		// utf-32 => wide character
		if (startwith(p, "U'")) {
			cur = cur->next = read_char_literal(p, p + 1, p_ty_uint());
			p += cur->len;
			continue;
		}

		// Identifier or keyword
		int ident_len = read_ident(p);
		if (ident_len) {
			cur = cur->next = new_token(TK_IDENT, p, p + ident_len);
			p += cur->len;
			continue;
		}

		// Punctuators
		int punct_len = read_punct(p);
		if (punct_len) {
			cur->next = new_token(TK_PUNCT, p, p + punct_len);
			cur = cur->next;
			p += punct_len;
			continue;
		}

		error_at(p, "invalid token");
	}
	cur->next = new_token(TK_EOF, p, p);

	add_line_number(head.next);
	return head.next;
}

// Returns the contents of a given file.
static char *read_file(const char *path)
{
	FILE *fp;

	if (!strcmp(path, "-")) {
		// By convention, read from stdin if a given filename is "-"
		fp = stdin;
	} else {
		fp = fopen(path, "r");
		if (!fp)
			return NULL;
	}

	// Read the entire file.
	char *buf;
	size_t buflen;
	FILE *out = open_memstream(&buf, &buflen);

	while (1) {
		char buf2[4096];
		int n = fread(buf2, 1, sizeof(buf2), fp);
		if (!n)
			break;
		fwrite(buf2, 1, n, out);
	}

	if (fp != stdin)
		fclose(fp);

	// make sure that the last line is properly terminated with '\n'
	fflush(out);
	if (!buflen || buf[buflen - 1] != '\n')
		fputc('\n', out);
	// EOF
	fputc('\0', out);
	fclose(out);

	return buf;
}

struct File **get_input_files(void)
{
	return input_files;
}

struct File *new_file(const char *name, int file_no, const char *contents)
{
	struct File *file = calloc(1, sizeof(struct File));

	file->name = name;
	file->file_no = file_no;
	file->contents = contents;
	return file;
}

// Replaces \r or \r\n with \n.
static void canonicalize_newline(char *p)
{
	int i = 0, j = 0;

	while (p[i]) {
		if (p[i] == '\r' && p[i + 1] == '\n') {
			i += 2;
			p[j++] = '\n';
		} else if (p[i] == '\r') {
			i++;
			p[j++] = '\n';
		} else {
			p[j++] = p[i++];
		}
	}

	p[j] = '\0';
}

// Removes backslashes followed by a newline.
static void remove_backslash_newline(char *p)
{
	int i = 0, j = 0;
	// We want to keep the number of newline characters so that
	// the logical line number matches the physical one.
	// This counter maintain the number of newlines we have removed.
	int n = 0;

	while (p[i]) {
		if (p[i] == '\\' && p[i + 1] == '\n') {
			// skip
			i += 2;
			n++;
		} else if (p[i] == '\n') {
			p[j++] = p[i++];
			// append the skipped newline
			for (; n > 0; n--)
				p[j++] = '\n';
		} else {
			p[j++] = p[i++];
		}
	}

	for (; n > 0; n--)
		p[j++] = '\n';
	p[j] = '\0';
}

static uint32_t read_universal_char(char *p, int len)
{
	uint32_t c = 0;

	for (int i = 0; i < len; i++) {
		if (!isxdigit(p[i]))
			return 0;

		c = (c << 4) | from_hex(p[i]);
	}

	return c;
}

// Replace \u or \U escape sequences with corresponding UTF-8 bytes.
static void convert_universal_chars(char *p)
{
	char *q = p;

	while (*p) {
		if (startwith(p, "\\u")) {
			uint32_t c = read_universal_char(p + 2, 4);
			if (c) {
				p += 6;
				q += encode_utf8(q, c);
			} else {
				*q++ = *p++;
			}

		} else if (startwith(p, "\\U")) {
			uint32_t c = read_universal_char(p + 2, 8);
			if (c) {
				p += 10;
				q += encode_utf8(q, c);
			} else {
				*q++ = *p++;
			}

		} else if (p[0] == '\\') {
			// escape character
			*q++ = *p++;
			*q++ = *p++;

		} else {
			*q++ = *p++;
		}
	}

	*q = '\0';
}

struct Token *tokenize_file(const char *path)
{
	char *p = read_file(path);
	if (!p)
		return NULL;

	canonicalize_newline(p);
	remove_backslash_newline(p);
	convert_universal_chars(p);

	static int file_no;
	struct File *file = new_file(path, file_no + 1, p);

	// Save the filename for assembler .file directive.
	input_files = realloc(input_files, sizeof(char *) * (file_no + 2));
	input_files[file_no] = file;
	input_files[file_no + 1] = NULL;
	file_no++;

	return tokenize(file);
}
