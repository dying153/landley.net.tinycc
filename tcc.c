/*
 *  TCC - Tiny C Compiler
 * 
 *  Copyright (c) 2001 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdio.h>

#define TEXT_SIZE       20000
#define DATA_SIZE       2000
#define SYM_TABLE_SIZE  10000
#define VAR_TABLE_SIZE  4096

/* symbol management */
typedef struct Sym {
    int v;    /* symbol token */
    int t;    /* associated type */
    int c;    /* associated number */
    struct Sym *next; /* next related symbol */
    struct Sym *prev; /* prev symbol in stack */
} Sym;

#define SYM_STRUCT 0x40000000 /* struct/union/enum symbol space */
#define SYM_FIELD  0x20000000 /* struct/union field symbol space */

/* loc : local variable index
   glo : global variable index
   parm : parameter variable index
   ind : output code ptr
   rsym: return symbol
   prog: output code
   astk: arg position stack
*/
void *file;
int tok, tok1, rsym, 
    prog, ind, loc, glo, vt, 
    vc, *macro_stack, *macro_stack_ptr, line_num;
char *idtable, *idptr, *filename;
Sym *define_stack, *global_stack, *local_stack, *label_stack;

/* The current value can be: */
#define VT_CONST   0x0002  /* constant in vc */
#define VT_VAR     0x0004  /* value is in eax */
#define VT_LOCAL   0x0008  /* offset on stack */

#define VT_LVAL    0x0010  /* const or var is an lvalue */
#define VT_CMP     0x0020  /* the value is stored in processor flags (in vc) */
#define VT_FORWARD 0x0040  /* value is forward reference (only used for functions) */
#define VT_JMP     0x0080  /* value is the consequence of jmp. bit 0 is set if inv */

#define VT_LVALN   -17         /* ~VT_LVAL */

/*
 *
 * VT_FUNC indicates a function. The return type is the stored type. A
 * function pointer is stored as a 'char' pointer.
 *
 * If VT_PTRMASK is non nul, then it indicates the number of pointer
 * iterations to reach the basic type.
 *   
 * Basic types:
 *
 * VT_BYTE indicate a char
 * VT_UNSIGNED indicates unsigned type
 *
 * otherwise integer type is assumed.
 *  
 */

#define VT_BYTE     0x00001  /* byte type, HARDCODED VALUE */
#define VT_PTRMASK  0x00f00  /* pointer mask */
#define VT_PTRINC   0x00100  /* pointer increment */
#define VT_FUNC     0x01000  /* function type */
#define VT_UNSIGNED 0x02000  /* unsigned type */
#define VT_ARRAY    0x04000  /* array type (only used in parsing) */
#define VT_TYPE    0xffffff01  /* type mask */
#define VT_TYPEN   0x000000fe  /* ~VT_TYPE */
#define VT_FUNCN   -4097       /* ~VT_FUNC */

#define VT_EXTERN  0x008000   /* extern definition */
#define VT_STATIC  0x010000   /* static variable */

/* Special infos */
#define VT_ENUM    0x020000  /* enum definition */
#define VT_STRUCT  0x040000  /* struct/union definition */
#define VT_TYPEDEF 0x080000  /* typedef definition */
#define VT_STRUCT_SHIFT 20   /* structure/enum name shift (12 bits lefts) */

/* token values */
#define TOK_INT      256
#define TOK_VOID     257
#define TOK_CHAR     258
#define TOK_IF       259
#define TOK_ELSE     260
#define TOK_WHILE    261
#define TOK_BREAK    262
#define TOK_RETURN   263
#define TOK_DEFINE   264
#define TOK_MAIN     265
#define TOK_FOR      266
#define TOK_EXTERN   267
#define TOK_STATIC   268
#define TOK_UNSIGNED 269
#define TOK_GOTO     270
#define TOK_DO       271
#define TOK_CONTINUE 272
#define TOK_SWITCH   273
#define TOK_CASE     274

/* ignored types Must have contiguous values */
#define TOK_CONST    275
#define TOK_VOLATILE 276
#define TOK_LONG     277
#define TOK_REGISTER 278
#define TOK_SIGNED   279

#define TOK_FLOAT    280 /* unsupported */
#define TOK_DOUBLE   281 /* unsupported */

#define TOK_STRUCT   282
#define TOK_UNION    283
#define TOK_TYPEDEF  284
#define TOK_DEFAULT  285
#define TOK_ENUM     286

#define TOK_EQ 0x94 /* warning: depend on asm code */
#define TOK_NE 0x95 /* warning: depend on asm code */
#define TOK_LT 0x9c /* warning: depend on asm code */
#define TOK_GE 0x9d /* warning: depend on asm code */
#define TOK_LE 0x9e /* warning: depend on asm code */
#define TOK_GT 0x9f /* warning: depend on asm code */

#define TOK_LAND 0xa0
#define TOK_LOR  0xa1

#define TOK_DEC   0xa2
#define TOK_MID   0xa3 /* inc/dec, to void constant */
#define TOK_INC   0xa4
#define TOK_ARROW 0xa7 

#define TOK_SHL   0x01 
#define TOK_SHR   0x02 
  
/* assignement operators : normal operator or 0x80 */
#define TOK_A_MOD 0xa5
#define TOK_A_AND 0xa6
#define TOK_A_MUL 0xaa
#define TOK_A_ADD 0xab
#define TOK_A_SUB 0xad
#define TOK_A_DIV 0xaf
#define TOK_A_XOR 0xde
#define TOK_A_OR  0xfc
#define TOK_A_SHL 0x81
#define TOK_A_SHR 0x82

#ifdef TINY
#define expr_eq() expr()
#else
void sum();
void next();
void expr_eq();
void expr();
void decl();
#endif


int inp()
{
#if 0
    int c;
    c = fgetc(file);
    printf("c=%c\n", c);
    return c;
#else
    return fgetc(file);
#endif
}

int isid(c)
{
    return (c >= 'a' & c <= 'z') |
        (c >= 'A' & c <= 'Z') |
        c == '_';
}

int isnum(c)
{
    return c >= '0' & c <= '9';
}

#ifndef TINY
/* XXX: use stderr ? */
void error(char *msg)
{
    printf("%s:%d: %s\n", filename, line_num, msg);
    exit(1);
}

void expect(char *msg)
{
    printf("%s:%d: %s expected\n", filename, line_num, msg);
    exit(1);
}

void warning(char *msg)
{
    printf("%s:%d: warning: %s\n", filename, line_num, msg);
}

void skip(c)
{
    if (tok != c) {
        printf("%s:%d: '%c' expected\n", filename, line_num, c);
        exit(1);
    }
    next();
}

void test_lvalue()
{
    if (!(vt & VT_LVAL))
        expect("lvalue");
}

#else

#define skip(c) next()
#define test_lvalue() 

#endif

char *get_tok_str(int v)
{
    int t;
    char *p;
    p = idtable;
    t = 256;
    while (t != v) {
        if (p >= idptr)
            return 0;
        while (*p++);
        t++;
    }
    return p;
}

/* find a symbol and return its associated structure. 's' is the top
   of the symbol stack */
Sym *sym_find1(Sym *s, int v)
{
    while (s) {
        if (s->v == v)
            return s;
        s = s->prev;
    }
    return 0;
}

Sym *sym_push1(Sym **ps, int v, int t, int c)
{
    Sym *s;
    s = malloc(sizeof(Sym));
    if (!s)
        error("memory full");
    s->v = v;
    s->t = t;
    s->c = c;
    s->next = 0;
    s->prev = *ps;
    *ps = s;
    return s;
}

/* find a symbol in the right symbol space */
Sym *sym_find(int v)
{
    Sym *s;
    s = sym_find1(local_stack, v);
    if (!s)
        s = sym_find1(global_stack, v);
    return s;
}

/* push a given symbol on the symbol stack */
Sym *sym_push(int v, int t, int c)
{
    //    printf("sym_push: %s type=%x\n", get_tok_str(v), t);
    if (local_stack)
        return sym_push1(&local_stack, v, t, c);
    else
        return sym_push1(&global_stack, v, t, c);
}

/* pop symbols until top reaches 'b' */
void sym_pop(Sym **ps, Sym *b)
{
    Sym *s, *ss;

    s = *ps;
    while(s != b) {
        ss = s->prev;
        //        printf("sym_pop: %s type=%x\n", get_tok_str(s->v), s->t);
        free(s);
        s = ss;
    }
    *ps = b;
}


void next()
{
    int c, v;
    char *q, *p;
    Sym *s;

    /* special 'ungettok' case for label parsing */
    if (tok1) {
        tok = tok1;
        tok1 = 0;
        return;
    }

    while(1) {
        c = inp();
#ifndef TINY
        if (c == '/') {
            /* comments */
            c = inp();
            if (c == '/') {
                /* single line comments */
                while (c != '\n')
                    c = inp();
            } else if (c == '*') {
                /* comments */
                while ((c = inp()) >= 0) {
                    if (c == '*') {
                        c = inp();
                        if (c == '/') {
                            c = ' ';
                            break;
                        } else if (c == '*')
                            ungetc(c, file);
                    } else if (c == '\n')
                        line_num++;
                }
            } else {
                ungetc(c, file);
                c = '/';
                break;
            }
        } else
#endif
        if (c == 35) {
            /* preprocessor: we handle only define */
            next();
            if (tok == TOK_DEFINE) {
                next();
                /* now tok is the macro symbol */
                sym_push1(&define_stack, tok, 0, ftell(file));
            }
            /* ignore preprocessor or shell */
            while (c != '\n')
                c = inp();
        }
        if (c == '\n') {
            /* end of line : check if we are in macro state. if so,
               pop new file position */
            if (macro_stack_ptr > macro_stack)
                fseek(file, *--macro_stack_ptr, 0);
            else
                line_num++;
        } else if (c != ' ' & c != 9)
            break;
    }
    if (isid(c)) {
        q = idptr;
        while(isid(c) | isnum(c)) {
            *q++ = c;
            c = inp();
        }
        *q++ = '\0';
        ungetc(c, file);
        p = idtable;
        tok = 256;
        while (p < idptr) {
            if (strcmp(p, idptr) == 0)
                break;
            while (*p++);
            tok++;
        }
        /* if not found, add symbol */
        if (p == idptr)
            idptr = q;
        /* eval defines */
        if (s = sym_find1(define_stack, tok)) {
            *macro_stack_ptr++ = ftell(file);
            fseek(file, s->c, 0);
            next();
        }
    } else {
#ifdef TINY
        q = "<=\236>=\235!=\225++\244--\242==\224";
#else
        q = "<=\236>=\235!=\225&&\240||\241++\244--\242==\224<<\1>>\2+=\253-=\255*=\252/=\257%=\245&=\246^=\336|=\374->\247";
#endif
        /* two chars */
        v = inp();
        while (*q) {
            if (*q == c & q[1] == v) {
                tok = q[2] & 0xff;
                if (tok == TOK_SHL | tok == TOK_SHR) {
                    v = inp();
                    if (v == '=')
                        tok = tok | 0x80;
                    else
                        ungetc(v, file);
                }
                return;
            }
            q = q + 3;
        }
        ungetc(v, file);
        /* single char substitutions */
        if (c == '<')
            tok = TOK_LT;
        else if (c == '>')
            tok = TOK_GT;
        else
            tok = c;
    }
}

void g(c)
{
    *(char *)ind++ = c;
}

void o(c)
{
    while (c) {
        g(c);
        c = c / 256;
    }
}

/* output a symbol and patch all calls to it */
void gsym_addr(t, a)
{
    int n;
    while (t) {
        n = *(int *)t; /* next value */
        *(int *)t = a - t - 4;
        t = n;
    }
}

void gsym(t)
{
    gsym_addr(t, ind);
}

/* psym is used to put an instruction with a data field which is a
   reference to a symbol. It is in fact the same as oad ! */
#define psym oad

/* instruction + 4 bytes data. Return the address of the data */
int oad(c, s)
{
    o(c);
    *(int *)ind = s;
    s = ind;
    ind = ind + 4;
    return s;
}

void vset(t, v)
{
    vt = t;
    vc = v;
}

/* generate a value in eax from vt and vc */
/* XXX: generate correct pointer for forward references to functions */
void gv()
{
#ifndef TINY
    int t;
#endif
    if (vt & VT_LVAL) {
        if ((vt & VT_TYPE) == VT_BYTE)
            o(0xbe0f);   /* movsbl x, %eax */
        else
            o(0x8b);     /* movl x,%eax */
        if (vt & VT_CONST)
            oad(0x05, vc);
        else if (vt & VT_LOCAL)
            oad(0x85, vc);
        else
            g(0x00);
    } else {
        if (vt & VT_CONST) {
            oad(0xb8, vc); /* mov $xx, %eax */
        } else if (vt & VT_LOCAL) {
            oad(0x858d, vc); /* lea xxx(%ebp), %eax */
        } else if (vt & VT_CMP) {
            oad(0xb8, 0); /* mov $0, %eax */
            o(0x0f); /* setxx %al */
            o(vc);
            o(0xc0);
        }
#ifndef TINY
        else if (vt & VT_JMP) {
            t = vt & 1;
            oad(0xb8, t); /* mov $1, %eax */
            oad(0xe9, 5); /* jmp after */
            gsym(vc);
            oad(0xb8, t ^ 1); /* mov $0, %eax */
        }
#endif
    }
    vt = (vt & VT_TYPE) | VT_VAR;
}

/* generate a test. set 'inv' to invert test */
/* XXX: handle constant */
int gtst(inv, t)
{
    if (vt & VT_CMP) {
        /* fast case : can jump directly since flags are set */
        g(0x0f);
        t = psym((vc - 16) ^ inv, t);
    } else 
#ifndef TINY
    if (vt & VT_JMP) {
        /* && or || optimization */
        if ((vt & 1) == inv)
            t = vc;
        else {
            t = psym(0xe9, t);
            gsym(vc);
        }
    } else 
    if ((vt & (VT_CONST | VT_LVAL)) == VT_CONST) {
        /* constant jmp optimization */
        if ((vc != 0) != inv) 
            t = psym(0xe9, t);
    } else
#endif
    {
        gv();
        o(0xc085); /* test %eax, %eax */
        g(0x0f);
        t = psym(0x85 ^ inv, t);
    }
    return t;
}

/* return type size. Put alignment at 'a' */
int type_size(int t, int *a)
{
    Sym *s;

    /* int, enum or pointer */
    if ((t & VT_PTRMASK) >= VT_PTRINC | 
        (t & VT_TYPE) == 0 |
        (t & VT_ENUM)) {
        *a = 4;
        return 4;
    } else if (t & VT_STRUCT) {
        /* struct/union */
        s = sym_find(((unsigned)t >> VT_STRUCT_SHIFT) | SYM_STRUCT);
        *a = 4; /* XXX: cannot store it yet. Doing that is safe */
        return s->c;
    } else {
        *a = 1;
        return 1;
    }
}

/* return the number size in bytes of a given type */
int incr_value(t)
{
    int a;

    if ((t & VT_PTRMASK) >= VT_PTRINC)
        return type_size(t - VT_PTRINC, &a);
    else
        return 1;
}

#define POST_ADD 0x1000
#define PRE_ADD  0

/* a defines POST/PRE add. c is the token ++ or -- */
void inc(a, c)
{
    test_lvalue();
    vt = vt & VT_LVALN;
    gv();
    o(0x018bc189); /* movl %eax, %ecx ; mov (%ecx), %eax */
    o(0x408d | a); /* leal x(%eax), %eax/%edx */
    g((c - TOK_MID) * incr_value(vt));
    o(0x0189 | a); /* mov %eax/%edx, (%ecx) */
}

/* XXX: handle ptr sub and 'int + ptr' case (only 'ptr + int' handled) */
/* XXX: handle constant propagation (need to track live eax) */
/* XXX: handle unsigned propagation */
void gen_op(op, l)
{
    int t;
    gv();
    t = vt;
    o(0x50); /* push %eax */
    next();
    if (l == -1)
        expr();
    else if (l == -2)
        expr_eq();
    else
        sum(l);
    gv();
    o(0x59); /* pop %ecx */
    if (op == '+' | op == '-') {
        /* XXX: incorrect for short (futur!) */
        if (incr_value(t) == 4)
            o(0x02e0c1); /* shl $2, %eax */
        if (op == '-') 
            o(0xd8f7); /* neg %eax */
        o(0xc801); /* add %ecx, %eax */
        vt = t;
    } else if (op == '&')
        o(0xc821);
    else if (op == '^')
        o(0xc831);
    else if (op == '|')
        o(0xc809);
    else if (op == '*')
        o(0xc1af0f); /* imul %ecx, %eax */
#ifndef TINY
    else if (op == TOK_SHL | op == TOK_SHR) {
        o(0xd391); /* xchg %ecx, %eax, shl/shr/sar %cl, %eax */
        if (op == TOK_SHL) 
            o(0xe0);
        else if (t & VT_UNSIGNED)
            o(0xe8);
        else
            o(0xf8);
    }
#endif
    else if (op == '/' | op == '%') {
        o(0x91);   /* xchg %ecx, %eax */
        if (t & VT_UNSIGNED) {
            o(0xd231); /* xor %edx, %edx */
            o(0xf1f7); /* div %ecx, %eax */
        } else {
            o(0xf9f799); /* cltd, idiv %ecx, %eax */
        }
        if (op == '%')
            o(0x92); /* xchg %edx, %eax */
    } else {
        o(0xc139); /* cmp %eax,%ecx */
        vset(VT_CMP, op);
    }
}

int expr_const()
{
    expr_eq();
    if ((vt & (VT_CONST | VT_LVAL)) != VT_CONST)
        expect("constant");
    return vc;
}

#ifndef TINY

/* enum/struct/union declaration */
int struct_decl(u)
{
    int a, t, b, v, size, align, maxalign, c;
    Sym *slast, *s, *ss;

    a = tok; /* save decl type */
    next();
    v = 0;
    if (tok != '{') {
        v = tok;
        next();
        /* struct already defined ? return it */
        /* XXX: check consistency */
        if (s = sym_find(v | SYM_STRUCT)) {
            if (s->t != a)
                error("invalid type");
            u = u | (v << VT_STRUCT_SHIFT);
            return u;
        }
    }
    s = sym_push(v | SYM_STRUCT, a, 0);
    /* put struct/union/enum name in type */
    u = u | (v << VT_STRUCT_SHIFT);
    
    if (tok == '{') {
        next();
        /* cannot be empty */
        c = 0;
        maxalign = 0;
        slast = 0;
        while (1) {
            if (a == TOK_ENUM) {
                v = tok;
                next();
                if (tok == '=') {
                    next();
                    c = expr_const();
                }
                sym_push(v, VT_CONST, c);
                if (tok == ',')
                    next();
                c++;
            } else {
                b = ist();
                while (1) {
                    t = typ(&v, b, &size);
                    if (t & (VT_FUNC | VT_TYPEDEF))
                        error("invalid type");
                    /* XXX: align & correct type size */
                    v |= SYM_FIELD;
                    size = type_size(t, &align);
                    if (a == TOK_STRUCT) {
                        c = (c + align - 1) & -align;
                        ss = sym_push(v, t, c);
                        c += size;
                    } else {
                        ss = sym_push(v, t, 0);
                        if (size > c)
                            c = size;
                    }
                    if (align > maxalign)
                        maxalign = align;
                    ss->next = slast;
                    slast = ss;
                    if (tok == ';' || tok == -1)
                        break;
                    skip(',');
                }
                skip(';');
            }
            if (tok == '}')
                break;
        }
        skip('}');
        s->next = slast;
        /* size for struct/union, dummy for enum */
        s->c = (c + maxalign - 1) & -maxalign; 
    }
    return u;
}
#endif

/* return 0 if no type declaration. otherwise, return the basic type
   and skip it. 
   XXX: A '2' is ored to ensure non zero return if int type.
 */
int ist()
{
    int t, n, v;
    Sym *s;

    t = 0;
    while(1) {
#ifndef TINY
        if (tok == TOK_ENUM) {
            t = struct_decl(VT_ENUM);
        } else if (tok == TOK_STRUCT || tok == TOK_UNION) {
            t = struct_decl(VT_STRUCT);
        } else
#endif
        {
            if (tok == TOK_CHAR | tok == TOK_VOID) {
                t |= VT_BYTE;
            } else if (tok == TOK_INT |
                       (tok >= TOK_CONST & tok <= TOK_SIGNED)) {
                /* ignored types */
            } else if (tok == TOK_FLOAT & tok == TOK_DOUBLE) {
                error("floats not supported");
            } else if (tok == TOK_EXTERN) {
                t |= VT_EXTERN;
            } else if (tok == TOK_STATIC) {
                t |= VT_STATIC;
            } else if (tok == TOK_UNSIGNED) {
                t |= VT_UNSIGNED;
            } else if (tok == TOK_TYPEDEF) {
                t |= VT_TYPEDEF;
            } else {
                s = sym_find(tok);
                if (!s || !(s->t & VT_TYPEDEF))
                    break;
                t = s->t & ~VT_TYPEDEF;
            }
            next();
        }
        t |= 2;
    }
    return t;
}

/* Read a type declaration (except basic type), and return the
   type. If v is true, then also put variable name in 'vc' */
int typ(int *v, int t, int *array_size_ptr)
{
    int u, p, n;

    t = t & -3; /* suppress the ored '2' */
    while (tok == '*') {
        next();
        t = t + VT_PTRINC;
    } 
    
    /* recursive type */
    /* XXX: incorrect if abstract type for functions (e.g. 'int ()') */
    if (tok == '(') {
        next();
        u = typ(v, 0, 0);
        skip(')');
    } else {
        u = 0;
        /* type identifier */
        if (v) {
            *v = tok;
            next();
        }
    }
    while(1) {
        if (tok == '(') {
            /* function declaration */
            next();
            /* push a dummy symbol to force local symbol stack usage */
            sym_push1(&local_stack, 0, 0, 0);
            p = 4; 
            while (tok != ')') {
                /* read param name and compute offset */
                if (t = ist())
                    t = typ(&n, t, 0); /* XXX: should accept both arg/non arg if v == 0 */
                else {
                    n = tok;
                    t = 0;
                    next();
                }
                p = p + 4;
                sym_push(n, VT_LOCAL | VT_LVAL | t, p);
                if (tok == ',')
                    next();
            }
            next(); /* skip ')' */
            if (u)
                t = u + VT_BYTE;
            else
                t = t | VT_FUNC;
        } else if (tok == '[') {
            /* array definition */
            if (t & VT_ARRAY) 
                error("multi dimension arrays not supported");
            next();
            n = 0;
            if (tok != ']') {
                n = expr_const();
                if (array_size_ptr)
                    *array_size_ptr = n;
            }
            if (n <= 0 & array_size_ptr != 0)
                error("invalid array size");
            skip(']');
            t = (t + VT_PTRINC) | VT_ARRAY;
        } else
            break;
    }
    return t;
}

/* define a new external reference to a function 'v' of type 'u' */
Sym *external_func(v, u)
{
    int t, n;
    Sym *s;
    s = sym_find(v);
    if (!s) {
        n = dlsym(0, get_tok_str(v));
        if (n == 0) {
            /* used to generate symbol list */
            s = sym_push1(&global_stack, 
                          v, u | VT_CONST | VT_LVAL | VT_FORWARD, 0);
        } else {
            /* int f() */
            s = sym_push1(&global_stack,
                          v, u | VT_CONST | VT_LVAL, n);
        }
    }
    return s;
}

/* read a number in base b */
int getn(c, b)
{
    int n, t;
    n = 0;
#ifndef TINY
    while (1) {
        if (c >= 'a')
            t = c - 'a' + 10;
        else if (c >= 'A')
            t = c - 'A' + 10;
        else
            t = c - '0';
        if (t < 0 | t >= b)
            break;
        n = n * b + t;
        c = inp();
    }
#else
    while (isnum(c)) {
        n = n * b + c - '0';
        c = inp();
    }
#endif
    ungetc(c, file);
    return n;
}

int getq(n)
{
    if (n == '\\') {
        n = inp();
        if (n == 'n')
            n = '\n';
#ifndef TINY
        else if (n == 'r')
            n = '\r';
        else if (n == 't')
            n = '\t';
#endif
        else if (isnum(n))
            n = getn(n, 8);
    }
    return n;
}

void unary()
{
    int n, t, ft, fc, p;
    Sym *s;

    if (isnum(tok)) {
        /* number */
#ifndef TINY
        t = 10;
        if (tok == '0') {
            t = 8;
            tok = inp();
            if (tok == 'x') {
                t = 16;
                tok = inp();
            }
        }
        vset(VT_CONST, getn(tok, t));
#else
        vset(VT_CONST, getn(tok, 10));
#endif
        next();
    } else 
#ifndef TINY
    if (tok == '\'') {
        vset(VT_CONST, getq(inp()));
        next(); /* skip char */
        skip('\''); 
    } else 
#endif
    if (tok == '\"') {
        vset(VT_CONST | VT_PTRINC | VT_BYTE, glo);
        while (tok == '\"') {
            while((n = inp()) != 34) {
                *(char *)glo++ = getq(n);
            }
            next();
        }
        *(char *)glo++ = 0;
    } else {
        t = tok;
        next();
        if (t == '(') {
            /* cast ? */
            if (t = ist()) {
                ft = typ(0, t, 0);
                skip(')');
                unary();
                vt = (vt & VT_TYPEN) | ft;
            } else {
                expr();
                skip(')');
            }
        } else if (t == '*') {
            unary();
            if (vt & VT_LVAL)
                gv();
#ifndef TINY
            if (!(vt & VT_PTRMASK))
                expect("pointer");
#endif
            vt = (vt - VT_PTRINC) | VT_LVAL;
        } else if (t == '&') {
            unary();
            test_lvalue();
            vt = (vt & VT_LVALN) + VT_PTRINC;
        } else
#ifndef TINY
        if (t == '!') {
            unary();
            if (vt & VT_CMP)
                vc = vc ^ 1;
            else
                vset(VT_JMP, gtst(1, 0));
        } else 
        if (t == '~') {
            unary();
            if ((vt & (VT_CONST | VT_LVAL)) == VT_CONST)
                vc = ~vc;
            else {
                gv();
                o(0xd0f7);
            }
        } else 
        if (t == '+') {
            unary();
        } else 
#endif
        if (t == TOK_INC | t == TOK_DEC) {
            unary();
            inc(PRE_ADD, t);
        } else if (t == '-') {
            unary();
            if ((vt & (VT_CONST | VT_LVAL)) == VT_CONST)
                vc = -vc;
            else {
                gv();
                o(0xd8f7); /* neg %eax */
            }
        } else 
        {
            s = sym_find(t);
            if (!s) {
                if (tok != '(')
                    error("undefined symbol");
                /* for simple function calls, we tolerate undeclared
                   external reference */
                s = external_func(t, VT_FUNC); /* int() function */
            }
            vset(s->t, s->c);
            /* if forward reference, we must point to s->c */
            if (vt & VT_FORWARD)
                vc = (int)&s->c;
        }
    }
    
    /* post operations */
    while (1) {
        if (tok == TOK_INC | tok == TOK_DEC) {
            inc(POST_ADD, tok);
            next();
        } else if (tok == '.' | tok == TOK_ARROW) {
            /* field */ 
            if (tok == '.') {
                test_lvalue();
                vt = (vt & VT_LVALN) + VT_PTRINC;
            }
            next();
            /* expect pointer on structure */
            if (!(vt & VT_STRUCT) || (vt & VT_PTRMASK) == 0)
                expect("struct or union");
            s = sym_find(((unsigned)vt >> VT_STRUCT_SHIFT) | SYM_STRUCT);
            /* find field */
            tok |= SYM_FIELD;
            while (s = s->next) {
                if (s->v == tok)
                    break;
            }
            if (!s)
                error("field not found");
            /* add field offset to pointer */
            gv();
            if (s->c)
                oad(0x05, s->c);
            /* change type to field type, and set to lvalue */
            vt = (vt & VT_TYPEN) | VT_LVAL | s->t;
            next();
        } else if (tok == '[') {
#ifndef TINY
            if (!(vt & VT_PTRMASK))
                expect("pointer");
#endif
            gen_op('+', -1);
            /* dereference pointer */
            vt = (vt - VT_PTRINC) | VT_LVAL;
            skip(']');
        } else if (tok == '(') {
            /* function call  */
            /* lvalue is implied */
            vt = vt & VT_LVALN;
            if ((vt & VT_CONST) == 0) {
                /* evaluate function address */
                gv();
                o(0x50); /* push %eax */
            }
            ft = vt;
            fc = vc;

            next();
            t = 0;
            while (tok != ')') {
                t = t + 4;
                expr_eq();
                gv();
                o(0x50); /* push %eax */
                if (tok == ',')
                    next();
            }
            skip(')');
            /* horrible, but needed : convert to native ordering (could
               parse parameters in reverse order, but would cost more
               code) */
            n = 0;
            p = t - 4;
            while (n < p) {
                oad(0x24848b, p); /* mov x(%esp,1), %eax */
                oad(0x248487, n); /* xchg   x(%esp,1), %eax */
                oad(0x248489, p); /* mov %eax, x(%esp,1) */
                n = n + 4;
                p = p - 4;
            }
            if (ft & VT_CONST) {
                /* forward reference */
                if (ft & VT_FORWARD) {
                    *(int *)fc = psym(0xe8, *(int *)fc);
                } else
                    oad(0xe8, fc - ind - 5);
                /* return value is variable, and take type from function proto */
                vt = VT_VAR | (ft & VT_TYPE & VT_FUNCN);
            } else {
                oad(0x2494ff, t); /* call *xxx(%esp) */
                t = t + 4;
                /* return value is variable, int */
                vt = VT_VAR;
            }
            if (t)
                oad(0xc481, t);
        } else {
            break;
        }
    }
}

void uneq()
{
    int ft, fc, b;
    
    unary();
    if (tok == '=' | 
        (tok >= TOK_A_MOD & TOK_A_DIV) |
        tok == TOK_A_XOR | tok == TOK_A_OR | 
        tok == TOK_A_SHL | tok == TOK_A_SHR) {
        test_lvalue();
        fc = vc;
        ft = vt;
        b = (vt & VT_TYPE) == VT_BYTE;
        if (ft & VT_VAR)
            o(0x50); /* push %eax */
        if (tok == '=') {
            next();
            expr_eq();
#ifndef TINY
            if ((vt & VT_PTRMASK) != (ft & VT_PTRMASK))
                warning("incompatible type");
#endif
            gv();  /* generate value */
        } else
            gen_op(tok & 0x7f, -2); /* XXX: incorrect, must call expr_eq */
        
        if (ft & VT_VAR) {
            o(0x59); /* pop %ecx */
            o(0x0189 - b); /* mov %eax/%al, (%ecx) */
        } else if (ft & VT_LOCAL)
            oad(0x8589 - b, fc); /* mov %eax/%al,xxx(%ebp) */
        else
            oad(0xa3 - b, fc); /* mov %eax/%al,xxx */
    }
}

void sum(l)
{
#ifndef TINY
    int t;
#endif
    if (l == 0)
        uneq();
    else {
        sum(--l);
        while ((l == 0 & (tok == '*' | tok == '/' | tok == '%')) |
               (l == 1 & (tok == '+' | tok == '-')) |
#ifndef TINY
               (l == 2 & (tok == TOK_SHL | tok == TOK_SHR)) |
#endif
               (l == 3 & (tok >= TOK_LT & tok <= TOK_GT)) |
               (l == 4 & (tok == TOK_EQ | tok == TOK_NE)) |
               (l == 5 & tok == '&') |
               (l == 6 & tok == '^') |
               (l == 7 & tok == '|')) {
            gen_op(tok, l);
       }
    }
}

#ifdef TINY 
void expr()
{
    sum(8);
}
#else
void eand()
{
    int t;

    sum(8);
    t = 0;
    while (1) {
        if (tok != TOK_LAND) {
            if (t) {
                t = gtst(1, t);
                vset(VT_JMP | 1, t);
            }
            break;
        }
        t = gtst(1, t);
        next();
        sum(8);
    }
}

void eor()
{
    int t, u;

    eand();
    t = 0;
    while (1) {
        if (tok != TOK_LOR) {
            if (t) {
                t = gtst(0, t);
                vset(VT_JMP, t);
            }
            break;
        }
        t = gtst(0, t);
        next();
        eand();
    }
}

void expr_eq()
{
    int t, u;
    
    eor();
    if (tok == '?') {
        next();
        t = gtst(1, 0);
        expr();
        gv();
        skip(':');
        u = psym(0xe9, 0);
        gsym(t);
        expr_eq();
        gv();
        gsym(u);
    }
}

void expr()
{
    while (1) {
        expr_eq();
        if (tok != ',')
            break;
        next();
    }
}

#endif

void block(int *bsym, int *csym, int *case_sym, int *def_sym)
{
    int a, b, c, d;
    Sym *s;

    if (tok == TOK_IF) {
        /* if test */
        next();
        skip('(');
        expr();
        skip(')');
        a = gtst(1, 0);
        block(bsym, csym, case_sym, def_sym);
        c = tok;
        if (c == TOK_ELSE) {
            next();
            d = psym(0xe9, 0); /* jmp */
            gsym(a);
            block(bsym, csym, case_sym, def_sym);
            gsym(d); /* patch else jmp */
        } else
            gsym(a);
    } else if (tok == TOK_WHILE) {
        next();
        d = ind;
        skip('(');
        expr();
        skip(')');
        a = gtst(1, 0);
        b = 0;
        block(&a, &b, case_sym, def_sym);
        oad(0xe9, d - ind - 5); /* jmp */
        gsym(a);
        gsym_addr(b, d);
    } else if (tok == '{') {
        next();
        /* declarations */
        s = local_stack;
        decl(VT_LOCAL);
        while (tok != '}')
            block(bsym, csym, case_sym, def_sym);
        /* pop locally defined symbols */
        sym_pop(&local_stack, s);
        next();
    } else if (tok == TOK_RETURN) {
        next();
        if (tok != ';') {
            expr();
            gv();
        }
        skip(';');
        rsym = psym(0xe9, rsym); /* jmp */
    } else if (tok == TOK_BREAK) {
        /* compute jump */
        if (!bsym)
            error("cannot break");
        *bsym = psym(0xe9, *bsym);
        next();
        skip(';');
    } else if (tok == TOK_CONTINUE) {
        /* compute jump */
        if (!csym)
            error("cannot continue");
        *csym = psym(0xe9, *csym);
        next();
        skip(';');
    } else 
#ifndef TINY
    if (tok == TOK_FOR) {
        int e;
        next();
        skip('(');
        if (tok != ';')
            expr();
        skip(';');
        d = ind;
        c = ind;
        a = 0;
        b = 0;
        if (tok != ';') {
            expr();
            a = gtst(1, 0);
        }
        skip(';');
        if (tok != ')') {
            e = psym(0xe9, 0);
            c = ind;
            expr();
            oad(0xe9, d - ind - 5); /* jmp */
            gsym(e);
        }
        skip(')');
        block(&a, &b, case_sym, def_sym);
        oad(0xe9, c - ind - 5); /* jmp */
        gsym(a);
        gsym_addr(b, c);
    } else 
    if (tok == TOK_DO) {
        next();
        a = 0;
        b = 0;
        d = ind;
        block(&a, &b, case_sym, def_sym);
        skip(TOK_WHILE);
        skip('(');
        gsym(b);
        expr();
        c = gtst(0, 0);
        gsym_addr(c, d);
        skip(')');
        gsym(a);
    } else
    if (tok == TOK_SWITCH) {
        next();
        skip('(');
        expr();
        gv();
        skip(')');
        a = 0;
        b = 0;
        c = 0;
        block(&a, csym, &b, &c);
        /* if no default, jmp after switch */
        if (c == 0)
            c = ind;
        /* default label */
        gsym_addr(b, c);
        /* break label */
        gsym(a);
    } else
    if (tok == TOK_CASE) {
        next();
        a = expr_const();
        if (!case_sym)
            expect("switch");
        gsym(*case_sym);
        oad(0x3d, a); /* cmp $xxx, %eax */
        *case_sym = psym(0x850f, 0); /* jne xxx */
        skip(':');
        block(bsym, csym, case_sym, def_sym);
    } else 
    if (tok == TOK_DEFAULT) {
        next();
        skip(':');
        if (!def_sym)
            expect("switch");
        if (*def_sym)
            error("too many 'default'");
        *def_sym = ind;
        block(bsym, csym, case_sym, def_sym);
    } else
    if (tok == TOK_GOTO) {
        next();
        s = sym_find1(label_stack, tok);
        /* put forward definition if needed */
        if (!s)
            s = sym_push1(&label_stack, tok, VT_FORWARD, 0);
        /* label already defined */
        if (s->t & VT_FORWARD) 
            s->c = psym(0xe9, s->c); /* jmp xxx */
        else
            oad(0xe9, s->c - ind - 5); /* jmp xxx */
        next();
        skip(';');
    } else
#endif
    {
        b = tok;
        next();
        if (tok == ':') {
            next();
            /* label case */
            s = sym_find1(label_stack, b);
            if (s) {
                if (!(s->t & VT_FORWARD))
                    error("multiple defined label");
                gsym(s->c);
                s->c = ind;
                s->t = 0;
            } else {
                sym_push1(&label_stack, b, 0, ind);
            }
            block(bsym, csym, case_sym, def_sym);
        } else {
            /* expression case: go backward of one token */
            /* XXX: currently incorrect if number/string/char */
            tok1 = tok;
            tok = b;
            if (tok != ';') {
                expr();
            }
            skip(';');
        }
    }
}

/* 'l' is VT_LOCAL or VT_CONST to define default storage type */
void decl(l)
{
    int *a, t, b, s, align, v, u, n;
    Sym *sym, *slocal;

    while (b = ist()) {
        if ((b & (VT_ENUM | VT_STRUCT)) && tok == ';') {
            /* we accept no variable after */
            next();
            continue;
        }
        while (1) { /* iterate thru each declaration */
            s = 1;
            slocal = local_stack; /* save local stack position, to restore it */
            t = typ(&v, b, &s);
            if (tok == '{') {
                /* patch forward references */
                if ((sym = sym_find(v)) && (sym->t & VT_FORWARD)) {
                    gsym(sym->c);
                    sym->c = ind;
                    sym->t = VT_CONST | VT_LVAL | t;
                } else {
                    /* put function address */
                    sym_push1(&global_stack, v, VT_CONST | VT_LVAL | t, ind);
                }
                loc = 0;
                o(0xe58955); /* push   %ebp, mov    %esp, %ebp */
                a = (int *)oad(0xec81, 0); /* sub $xxx, %esp */
                rsym = 0;
                block(0, 0, 0, 0);
                gsym(rsym);
                o(0xc3c9); /* leave, ret */
                *a = (-loc + 3) & -4; /* align local size to word & 
                                         save local variables */
                sym_pop(&label_stack, 0); /* reset label stack */
                sym_pop(&local_stack, 0); /* reset local stack */
                break;
            } else {
                /* reset local stack (needed because of dummy function
                   parameters */
                sym_pop(&local_stack, slocal);
                if (t & VT_TYPEDEF) {
                    /* save typedefed type */
                    sym_push(v, t, 0);
                } else if (t & VT_FUNC) {
                    /* external function definition */
                    external_func(v, t);
                } else {
                    /* not lvalue if array */
                    if (!(t & VT_ARRAY))
                        t |= VT_LVAL;
                    if (t & VT_EXTERN) {
                        /* external variable */
                        /* XXX: factorize with external function def */
                        n = dlsym(NULL, get_tok_str(v));
                        if (!n)
                            error("unknown external variable");
                        sym_push(v, VT_CONST | t, n);
                    } else {
                        u = l;
                        if (t & VT_STATIC)
                            u = VT_CONST;
                        u |= t;
                        if (t & VT_ARRAY)
                            t -= VT_PTRINC; 
                        align = type_size(t, &align);
                        s *= align;
                        if (u & VT_LOCAL) {
                            /* allocate space down on the stack */
                            loc = (loc - s) & -align;
                            sym_push(v, u, loc);
                        } else {
                            /* allocate space up in the data space */
                            glo = (glo + align - 1) & -align;
                            sym_push(v, u, glo);
                            glo += s;
                        }
                    }
                }
                if (tok != ',') {
                    skip(';');
                    break;
                }
                next();
            }
        }
    }
}

int main(int c, char **v)
{
    Sym *s;
    int (*t)();
    if (c < 2) {
        printf("usage: tc src\n");
        return 1;
    }
    v++;
    filename = *v;
    file = fopen(filename, "r");
#ifndef TINY
    if (!file) {
        perror(filename);
        exit(1);
    }
#endif

    idtable = malloc(SYM_TABLE_SIZE);
#ifdef TINY
    memcpy(idtable, 
           "int\0void\0char\0if\0else\0while\0break\0return\0define\0main", 53);
    idptr = idtable + 53;
#else
    memcpy(idtable, 
           "int\0void\0char\0if\0else\0while\0break\0return\0define\0main\0for\0extern\0static\0unsigned\0goto\0do\0continue\0switch\0case\0const\0volatile\0long\0register\0signed\0float\0double\0struct\0union\0typedef\0default\0enum", 192);
    idptr = idtable + 192;
#endif
    glo = malloc(DATA_SIZE);
    prog = malloc(TEXT_SIZE);
    macro_stack = malloc(256);
    macro_stack_ptr = macro_stack;
    ind = prog;
    line_num = 1;
    next();
    decl(VT_CONST);
#ifdef TEST
    { 
        FILE *f;
        f = fopen(v[1], "w");
        fwrite((void *)prog, 1, ind - prog, f);
        fclose(f);
        return 0;
    }
#else
    s = sym_find(TOK_MAIN);
    if (!s)
        error("main() not defined");
    t = s->c;
    return (*t)(c - 1, v);
#endif
}
