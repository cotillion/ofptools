/*
 * Operation Flashpoint Binary to CPP converter.
 *
 * Will convert a binarized config.cpp or autosave.fps to
 * a human readable format.
 *
 * Copyright (C) 2004  Erik Gävert <erik<A>gavert.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

FILE *out;
char *string_table[20000];

/*
 * This is used to read string ids and sizes from 
 * the binary data.
 *
 * It appears to be done in a bit special way to save file space.
 */
int
read_int(FILE *f)
{
    unsigned char extra;
    int number;

    number = fgetc(f);
    if (number & 0x80)
    {
        extra = fgetc(f);
        number += (extra - 1) * 0x80;
    }

    return number;
}

/*
 * Reads a string id from the file stream, if it's unknown read
 * following string also.
 */
char *
read_string(FILE *f)
{
    char c, *ptr;
    int id;
    unsigned char extra;
    int i;
    char tmp[10240];

    id = read_int(f);

    /* I really don't know what the limit on this is. */
    if (id > sizeof(tmp))
    {
        printf("FATAL: String table is full\n");
        exit(1);
    }
    
    if (string_table[id])
    {
        return string_table[id];
    }    
    
    i = 0;
    while (1)
    {
        c = fgetc(f);
      
        if (c == EOF)
        {
            printf("EOF While Reading String\n");
            exit(1);
        }

        tmp[i] = c;
        if (c == '\0')
            break;
        i++;
    }

    ptr = malloc(strlen(tmp) + 1);
    strcpy(ptr, tmp);
    string_table[id] = ptr;
    return ptr;
}

/*
 * Read a string without stringtable magic.
 */
char *
simple_read_string(FILE *f)
{
    char c, *ptr;
    int i;
    char tmp[10240];
    
    i = 0;
    while (1)
    {
        c = fgetc(f);
        
        if (c == EOF)
        {
            printf("EOF While Reading String\n");
            exit(1);
        }

        if (i >= sizeof(tmp))
        {
            printf("FATAL: Simple string buffer full\n");
            exit(1);
        }
        
        tmp[i] = c;
        if (c == '\0')
            break;
        i++;
    }
    
    ptr = malloc(strlen(tmp) + 1);
    strcpy(ptr, tmp);
    return ptr;
}

void
output(int depth, char *fmt, ...)
{
    if (depth < 0)
        return;
    
    if (depth)
        fprintf(out, "%-*s", depth * 4, "");
    
    va_list ap;    
    va_start(ap, fmt);    
    vfprintf(out, fmt, ap);
    va_end(ap);
}

void
read_array(int depth, FILE *f)
{
    int size;
    char tmp;
    float fl;
    char *n1;
    int val;
    
    size = read_int(f);
    
    while (size--)
    {
        tmp = fgetc(f);
        switch (tmp)
        {
        case 0:
            n1 = read_string(f);
            output(0, "\"%s\"", n1);
            break;
        case 1:
            /* Float */
            fl = 0.0;
            fread(&fl, 1, 4, f);
            output(0, "%f", fl);
            break;
        case 2:
            fread(&val, 1, 4, f);
            output(0, "%d", val);
            break;
        case 3:
            /* Array, we really need to recurse.. */
            output(0, "\n");
            output(depth, "{ ");
            read_array(depth + 1, f);
            output(0, " }");
            break;
        default:
            printf("FATAL: Unknown Variable Type %d\n", tmp);
            exit(1);
        }
        
        if (size)
            output(0, ", ");
        if (tmp == 3)
        {
            output(0, "\n");
            output(depth, "");
        }
    }
}

/*
 * <4 byte> commdand type
 * <string id> if unknown followed by
 *    <string> element name
 * 
 */
void *
read_entry(FILE *f, int depth) {
    char something;
    char *name, *n1, *n2;
    int children, size;
    unsigned char type, tmp;
    int val;
    float fl;
    char slask[128];
    
    type = fgetc(f);

    switch (type)
    {
    case 0:
        /* Class definition */
        output(0, "\n");
        
        name = read_string(f);
        n1 = simple_read_string(f);

        if (strlen(n1))
        {
            output(depth, "class %s: %s {\n", name, n1);
        } else {
            output(depth, "class %s {\n", name);
        }

        /* There is odd magic here */
        children = read_int(f);

        /* Read the children */
        while (children--)
            read_entry(f, depth + 1);
        output(depth, "};\n\n");
        
        break;
    case 1:
        /* Variable Assigment */
        tmp = fgetc(f);

        n1 = read_string(f);

        switch (tmp)
        {

        case 0:
            n2 = read_string(f);
            output(depth, "%s = \"%s\";\n", n1, n2);
            break;
        case 1:
            /* Float */
            fread(&fl, 1, 4, f);
            output(depth, "%s = %f;\n", n1, fl);
            break;
        case 2:
            fread(&val, 1, 4, f);
            output(depth, "%s = %d;\n", n1, val);
            break;
        default:
            printf("Unknown Variable Type: %d\n", tmp);
            exit(1);
        }
        break;
    case 2:
        /* Array Assignment */
        n1 = read_string(f);
        output(depth, "%s[] = { ", n1);
        read_array(depth + 1, f);        
        output(0, "};\n");
        break;
    case 99:
        /* This is a case that should be somwhere, not sure if its ever used */
        fread(slask, 1, 3, f);

        while (1)
        {
            name = simple_read_string(f);
            fread(&val, 1, 4, f);
            output(depth, "#define %s\t%d\n", name, val);
        }
        break;
    default:
        printf("Unknown type: %X\n", type);
        exit(1);
        break;
    }
}

int
main(int argc, char *argv[])
{
    FILE *f;

    if (argc < 2)
    {
        printf("Syntax: %s <bin-file>\n", argv[0]);
        return 0;
    }
  
    if ((f = fopen(argv[1], "r")) == NULL)
    {
        perror("Unable to open file");
        return 1;
    }

    /* 0 the string table */
    memset(string_table, '\0', sizeof(string_table));
    
    fseek(f, 7, SEEK_SET);

    //out = fopen("RESULT", "w"); 
    out = stdout;
    read_entry(f, 0);
}
