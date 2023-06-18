#include <toycc.h>
#include <type.h>

// input file
static struct File *current_file;

// A list of all input files
static struct File **input_files;

static void __attribute__((noreturn))
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
	struct Token *tok = malloc(sizeof(struct Token));

	tok->kind = kind;
	tok->loc = start;
	tok->len = end - start;
	tok->file = current_file;
	tok->at_bol = at_bol;
	at_bol = false;
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
	};

	for (size_t i = 0; i < ARRAY_SIZE(kw); i++)
		if (startwith(p, kw[i]))
			return strlen(kw[i]);

	return ispunct(*p) ? 1 : 0;
}

// Returns true if c is valid as the first character of an identifier.
static bool is_ident1(char c)
{
	return isalpha(c) || c == '_';
}

// Returns true if c is valid as a non-first character of an identifier.
static bool is_ident2(char c)
{
	return is_ident1(c) || isdigit(c);
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

static struct Token *read_string_literal(const char *start)
{
	const char *end = string_literal_end(start + 1);
	char *buf = malloc(sizeof(char) * (end - start));
	int len = 0;

	// skip '"'
	for (const char *p = start + 1; p < end;) {
		if (*p == '\\')
			buf[len++] = read_escaped_char(&p, p + 1);
		else
			buf[len++] = *p++;
	}
	// terminated with '\0'
	buf[len] = '\0';

	struct Token *tok = new_token(TK_STR, start, end + 1);
	// string + '\0'
	tok->ty = array_of(p_ty_char(), len + 1);
	tok->str = buf;
	return tok;
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

void convert_keywords(struct Token *tok)
{
	for (struct Token *t = tok; t->kind != TK_EOF; t = t->next)
		if (is_keyword(t))
			t->kind = TK_KEYWORD;
}

static struct Token *read_char_literal(const char *start)
{
	const char *p = start + 1;
	if (*p == '\0')
		error_at(start, "unclosed char literal");

	char c;
	if (*p == '\\')
		c = read_escaped_char(&p, p + 1);
	else
		c = *p++;

	const char *end = strchr(p, '\'');
	if (!end)
		error_at(p, "unclosed char literal");

	// skip '\''
	struct Token *tok = new_token(TK_NUM, start, end + 1);
	tok->val = c;
	tok->ty = p_ty_int();
	return tok;
}

static struct Token *read_int_literal(const char *start)
{
	const char *p = start;
	int base = 10;

	// Read a binary, octal, decimal or hexadecimal number.
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

	struct Token *tok = new_token(TK_NUM, start, p);
	tok->val = val;
	tok->ty = ty;
	return tok;
}

static struct Token *read_number(const char *start)
{
	// Try to parse as an integer constant
	struct Token *tok = read_int_literal(start);

	if (!strchr(".eEfF", start[tok->len]))
		return tok;

	// If it's not an integer, it must be a floating point constant
	char *end;
	double val = strtod(start, &end);

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

	tok = new_token(TK_NUM, start, end);
	tok->fval = val;
	tok->ty = ty;
	return tok;
}

// Tokenize a given string and returns new tokens.
static struct Token *tokenize(struct File *file)
{
	const char *p = file->contents;
	struct Token head;
	struct Token *cur = &head;

	current_file = file;
	at_bol = true;

	while (*p) {
		// Skip line comments
		if (startwith(p, "//")) {
			p += 2;
			while (*p != '\n')
				p++;
			continue;
		}

		// Skip block comments
		if (startwith(p, "/*")) {
			const char *q = strstr(p + 2, "*/");
			if (!q)
				error_at(p, "unclosed block comment");
			p = q + 2;
			continue;
		}

		// skip newline
		if (*p == '\n') {
			p++;
			at_bol = true;
			continue;
		}

		// Skip whitespace characters
		if (isspace(*p)) {
			p++;
			continue;
		}

		// Numeric literal
		if (isdigit(*p) || (*p == '.' && isdigit(p[1]))) {
			cur->next = read_number(p);
			cur = cur->next;
			p += cur->len;
			continue;
		}

		// string literal
		if (*p == '"') {
			cur->next = read_string_literal(p);
			cur = cur->next;
			p += cur->len;
			continue;
		}

		// character literal
		if (*p == '\'') {
			cur->next = read_char_literal(p);
			cur = cur->next;
			p += cur->len;
			continue;
		}

		// Identifier or keyword
		if (is_ident1(*p)) {
			const char *start = p;
			do {
				p++;
			} while (is_ident2(*p));
			cur->next = new_token(TK_IDENT, start, p);
			cur = cur->next;
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
static const char *read_file(const char *path)
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

static struct File *new_file(const char *name, int file_no, const char *contents)
{
	struct File *file = malloc(sizeof(struct File));
	file->name = name;
	file->file_no = file_no;
	file->contents = contents;
	return file;
}

struct Token *tokenize_file(const char *path)
{
	const char *p = read_file(path);
	if (!p)
		return NULL;

	static int file_no;
	struct File *file = new_file(path, file_no + 1, p);

	// Save the filename for assembler .file directive.
	input_files = realloc(input_files, sizeof(char *) * (file_no + 2));
	input_files[file_no] = file;
	input_files[file_no + 1] = NULL;
	file_no++;

	return tokenize(file);
}
