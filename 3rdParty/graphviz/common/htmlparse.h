/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

#ifndef YY_HTML_HTMLPARSE_H_INCLUDED
# define YY_HTML_HTMLPARSE_H_INCLUDED
/* Debug traces.  */
#ifndef HTMLDEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define HTMLDEBUG 1
#  else
#   define HTMLDEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define HTMLDEBUG 0
# endif /* ! defined YYDEBUG */
#endif  /* ! defined HTMLDEBUG */
#if HTMLDEBUG
extern int htmldebug;
#endif

/* Token type.  */
#ifndef HTMLTOKENTYPE
# define HTMLTOKENTYPE
  enum htmltokentype
  {
    T_end_br = 258,
    T_end_img = 259,
    T_row = 260,
    T_end_row = 261,
    T_html = 262,
    T_end_html = 263,
    T_end_table = 264,
    T_end_cell = 265,
    T_end_font = 266,
    T_string = 267,
    T_error = 268,
    T_n_italic = 269,
    T_n_bold = 270,
    T_n_underline = 271,
    T_n_overline = 272,
    T_n_sup = 273,
    T_n_sub = 274,
    T_n_s = 275,
    T_HR = 276,
    T_hr = 277,
    T_end_hr = 278,
    T_VR = 279,
    T_vr = 280,
    T_end_vr = 281,
    T_BR = 282,
    T_br = 283,
    T_IMG = 284,
    T_img = 285,
    T_table = 286,
    T_cell = 287,
    T_font = 288,
    T_italic = 289,
    T_bold = 290,
    T_underline = 291,
    T_overline = 292,
    T_sup = 293,
    T_sub = 294,
    T_s = 295
  };
#endif
/* Tokens.  */
#define T_end_br 258
#define T_end_img 259
#define T_row 260
#define T_end_row 261
#define T_html 262
#define T_end_html 263
#define T_end_table 264
#define T_end_cell 265
#define T_end_font 266
#define T_string 267
#define T_error 268
#define T_n_italic 269
#define T_n_bold 270
#define T_n_underline 271
#define T_n_overline 272
#define T_n_sup 273
#define T_n_sub 274
#define T_n_s 275
#define T_HR 276
#define T_hr 277
#define T_end_hr 278
#define T_VR 279
#define T_vr 280
#define T_end_vr 281
#define T_BR 282
#define T_br 283
#define T_IMG 284
#define T_img 285
#define T_table 286
#define T_cell 287
#define T_font 288
#define T_italic 289
#define T_bold 290
#define T_underline 291
#define T_overline 292
#define T_sup 293
#define T_sub 294
#define T_s 295

/* Value type.  */
#if ! defined HTMLSTYPE && ! defined HTMLSTYPE_IS_DECLARED

union HTMLSTYPE
{
  int    i;
  htmltxt_t*  txt;
  htmlcell_t*  cell;
  htmltbl_t*   tbl;
  textfont_t*  font;
  htmlimg_t*   img;
  pitem*       p;
};

typedef union HTMLSTYPE HTMLSTYPE;
# define HTMLSTYPE_IS_TRIVIAL 1
# define HTMLSTYPE_IS_DECLARED 1
#endif


extern HTMLSTYPE htmllval;

// extern int htmlparse (void);

#endif /* !YY_HTML_HTMLPARSE_H_INCLUDED  */
