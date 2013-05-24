/***
*    Copyright (C) 1999,2000  Dibyendu Majumdar.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*    Author : Dibyendu Majumdar
*    Email  : dibyendu@mazumdar.demon.co.uk
*    Website: www.mazumdar.demon.co.uk
*/
#ifndef list_h
#define list_h

// #include "sys/queue.h"

typedef struct link_t {
	TAILQ_ENTRY(link_t) entries;
} link_t ;

typedef struct list_t {
	TAILQ_HEAD(lhead_t, link_t) lhead;
} list_t ;

#define list_init(list) TAILQ_INIT(&(list)->lhead)
#define list_first(list) TAILQ_FIRST(&(list)->lhead)
#define list_next(list, elem) TAILQ_NEXT(elem, entries)
#define list_append(list, elem) TAILQ_INSERT_TAIL(&(list)->lhead, elem, entries)
#define list_remove(list, elem) TAILQ_REMOVE(&(list)->lhead, elem, entries)

#ifdef TAILQ_PREV
#undef TAILQ_PREV
#endif

#define TAILQ_PREV(elm, headname, field) \
        (*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))

#define list_prev(list, elem) TAILQ_PREV(elem, lhead_t, entries)

#endif
