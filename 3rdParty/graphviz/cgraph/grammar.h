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

#ifndef YY_AAG_GRAMMAR_H_INCLUDED
# define YY_AAG_GRAMMAR_H_INCLUDED
/* Debug traces.  */
#ifndef AAGDEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define AAGDEBUG 1
#  else
#   define AAGDEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define AAGDEBUG 0
# endif /* ! defined YYDEBUG */
#endif  /* ! defined AAGDEBUG */
#if AAGDEBUG
extern int aagdebug;
#endif

/* Token type.  */
#ifndef AAGTOKENTYPE
# define AAGTOKENTYPE
  enum aagtokentype
  {
    T_graph = 258,
    T_node = 259,
    T_edge = 260,
    T_digraph = 261,
    T_subgraph = 262,
    T_strict = 263,
    T_edgeop = 264,
    T_list = 265,
    T_attr = 266,
    T_atom = 267,
    T_qatom = 268
  };
#endif
/* Tokens.  */
#define T_graph 258
#define T_node 259
#define T_edge 260
#define T_digraph 261
#define T_subgraph 262
#define T_strict 263
#define T_edgeop 264
#define T_list 265
#define T_attr 266
#define T_atom 267
#define T_qatom 268

/* Value type.  */
#if ! defined AAGSTYPE && ! defined AAGSTYPE_IS_DECLARED

union AAGSTYPE
{
            int                i;
            char            *str;
            struct Agnode_s    *n;

};

typedef union AAGSTYPE AAGSTYPE;
# define AAGSTYPE_IS_TRIVIAL 1
# define AAGSTYPE_IS_DECLARED 1
#endif


extern AAGSTYPE aaglval;

int aagparse (void);

#endif /* !YY_AAG_GRAMMAR_H_INCLUDED  */
