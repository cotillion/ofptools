/*
 * PBO file format reader
 *
 * Handles both Flashpoint Resistance and Cold War Crisis formats.
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

#define VERSION "1.0"

#define VERSION_COLDWAR    1
#define VERSION_RESISTANCE 2

void unpack_data(char *data, size_t len, char *result);

struct pbo_file {
    u_int32_t version;
    u_int32_t file_count;
    long      file_size;
    long      real_file_size;
    long      data_offset;
    struct pbo_entry *files;
};

struct pbo_entry {
    char    *filename;
    char    *data;
    int32_t type;
    int32_t real_size;
    int32_t reserved;
    int32_t time;
    int32_t size;
    long    offset;
    struct pbo_file *container;    
    struct pbo_entry *next;
};

char *
read_string(FILE *f)
{
    char c, *ptr;
    int i;
    char tmp[256];

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
    return ptr;
}

struct pbo_entry * 
read_entry(FILE *f, int offset)
{
    struct pbo_entry *p;

    p = malloc(sizeof(struct pbo_entry));
    p->data = NULL;

    p->filename = read_string(f);
    
    /* If it's a stupid comment, we can't do much about it */
    if (!strcasecmp(p->filename, "product"))
    {
        printf("Product: %s\n", read_string(f));
        p->data = read_string(f);
        return p;
    }

    /* Pack Method */
    fread(&(p->type), 1, 4, f);
    /* Org Size */
    fread(&(p->real_size), 1, 4, f);
    /* Reserved */
    fread(&(p->reserved), 1, 4, f);
    /* Time */
    fread(&(p->time), 1, 4, f);
    /* Size */
    fread(&(p->size), 1, 4, f);
  
    return p;
}

struct pbo_file *
read_pbo(FILE *f)
{
    struct pbo_file *container;
    struct pbo_entry *p, *tail;
  
    container = malloc(sizeof(struct pbo_file));
    container->files = NULL;
    container->file_count = 0;
    container->file_size = 0L;
    container->real_file_size = 0L;  

    /* Start from the beginning */
    rewind(f);

    tail = NULL;
  
    while (1)
    {
        p = read_entry(f, 0);
        p->container = container;
        
        /* Empty entry, last one */
        if (tail && !strcmp(p->filename, ""))
        {
            tail->next = NULL;
            free(p);
            break;
        }
        
        if (container->files == NULL)
            container->files = p;
        if (tail != NULL)
            tail->next = p;

      
        p->offset = container->file_size;
        container->file_count += 1;
        container->file_size += p->size;
        container->real_file_size += p->real_size;
        tail = p;
    }

    /* Get the position, need it to read files later. */
    container->data_offset = ftell(f);
    
    printf("Total File Size: %ldb (%d files)\n", container->file_size,
        container->file_count);
    return container;
}

/*
 * Closes the pbo, freeing all allocated memory
 */
void
close_pbo(struct pbo_file *container)
{
    struct pbo_entry *ptr, *tmp;

    ptr = container->files;
    while (ptr != NULL)
    {
        free(ptr->filename);

        /* Some entries have some extra data, like product */
        if (ptr->data)
            free(ptr->data);
        
        tmp = ptr;
        ptr = ptr->next;
        free(tmp);        
    }

    free(container);
}

/*
 * Read pbo_entry into *data
 */
char *
read_pbo_entry(FILE *f, struct pbo_entry *entry, size_t *length)
{
    char *data, *space;

    fseek(f, entry->container->data_offset + entry->offset, SEEK_SET);

    data = malloc(entry->size);
    fread(data, entry->size, 1, f);

    /* Data might be packed */
    if (entry->real_size)
    {
        space = malloc(entry->real_size + 1);
        memset(space, 'A', entry->real_size);
        unpack_data(data, entry->size, space);
        free(data);
        *length = entry->real_size;
        return space;
    }
    
    *length = (size_t)entry->size;
    return data;
}

/*
 * Unpacks data into the memory space pointed to by result
 */
void
unpack_data(char *data, size_t len, char *result)
{
    char *ptr, *rptr;
    int offset;
    int length, tmp, count;
    unsigned int check1, check2;
    int rpos, rlen;
    int chunk, size;
    char index;

    unsigned char c;
    unsigned char b1, b2;

    size = 0;
    offset = 0;
    rptr = result;
  
    /*
     * Each data block is precded by a byte telling us what to do with
     * the next 8 bytes.
     */
    while (offset < (len - 4))
    {
        ptr = (data + offset);
        index = *ptr;

        offset += 1;
      
        count = 1;
        tmp = 1;
        length = 0;
        while (tmp < 256 && offset < (len - 4))
        {
            ptr = (data + offset);
            size = (rptr - result);
          
            if (index & tmp)
            {
                /* Append the byte to output */
                *rptr = *ptr;
                rptr += 1;
                
                // printf("Appending %c to output\n", *ptr);
                offset += 1;
            } else {
                /* Read a pointer */
                b1 = *ptr;
                b2 = *(ptr + 1);

                rpos = size - b1 - 256 * ( b2/16 );
                rlen = b2 - 16 * (b2 / 16) + 3;
                
                // printf("One annoying pointer (%d/%d/%d) (%d %d)\n", b1, b2,
                //    size, rpos, rlen);
                offset += 2;

                if ((rpos + rlen) < 0)
                {
                    // printf("Padding With spaces\n");
                    while (rlen--)
                    {
                        *rptr = ' ';
                        rptr++;
                        size++;
                    }
                }
                
                /* This is not so nice */
                else if ((result + rpos) > rptr)
                {
                    fprintf(stderr, "Pointer out of bounds\n");
                    exit(1);
                    //printf("Size: %d Rpos: %d\n", size, rpos);
                }              
                /* PAD the file with a block from another place in the file */
                else if ((rpos + rlen) <= size)
                {
                    /* I think we're supposed to add spaces then */
                    while (rpos < 0)
                    {
                        *rptr = ' ';
                        rptr++;
                        rlen--;
                        rpos++;                        
                    }
                    
                    // printf("Easy append %d (%d)\n", rlen, rpos);
                    memcpy(rptr, (void *)(result + rpos), rlen);
                    rptr += rlen;
                    size += rlen;

                }
              
                /* PAD the file with the block until size is rpos + rlen */
                else if ((rpos + rlen) > size)
                {
                    chunk = (size - rpos);
                    // printf("Size: %d Rpos: %d %d\n", size, rpos, chunk);
                    while (rlen > 0)
                    {
                        if (chunk > rlen)
                            chunk = rlen;

                        if (!chunk)
                        {
                            fprintf(stderr, "Chunk is 0\n");
                            exit(1);
                        }
                        
                        // printf("Appended %db %d %d\n", chunk, size,
                        // rpos + rlen);
                        memcpy(rptr, (void *)(result + rpos), chunk);
                        rlen -= chunk;
                        rptr += chunk;
                        size += chunk;
                    }
                } 
            }
            count += 1;
            tmp = tmp * 2;
        }
        offset += length;
    }

    /* Last 4 bytes of the packed data is the checumsum, unsigned int */
    memcpy(&check1, (void *)(data + len - 4), sizeof(check1));
    check2 = 0;
  
    ptr = result;
    while (ptr <= (result + size))
    {
        c = (unsigned char)*ptr;
        check2 += c;
        ptr++;
    }

    if (check1 == check2)
    {
        return;
    }

    fprintf(stderr, "Checksum Error (%d/%d)\n", check1, check2);    
}


int
main(int argc, char *argv[])
{
    size_t len;
    char *dir, *ptr, *file;
    struct pbo_file *pbo;
    struct pbo_entry *entry;
    
    FILE *f, *out;

    if (argc < 2)
    {
        printf("Syntax: %s <pbo-file>\n", argv[0]);
        return 0;
    }
  
    if ((f = fopen(argv[1], "r")) == NULL)
    {
        perror("Unable to open file");
        return 1;
    }

    printf("pboread %s by zyklone <zyklone@hotmail.com>\n", VERSION);
    
    dir = malloc(strlen(argv[1]) + 1);
    strcpy(dir, argv[1]);
    ptr = strrchr(dir, '/');
    if (ptr != NULL)
    {
        ptr++;
        dir = ptr;
    }
    
    ptr = strrchr(dir, '.');
    if (ptr == NULL)
    {
        fprintf(stderr, "Unable to make output directory, missing . in %s\n",
            dir);
        return 1;
    }
    
    *ptr = '\0';
    mkdir(dir, S_IRUSR | S_IWUSR | S_IXUSR );    
    
    pbo = read_pbo(f);
    entry = pbo->files;
    while (entry != NULL)
    {
        if (!strlen(entry->filename) || !strcmp(entry->filename, "product"))
        {
            entry = entry->next;
            continue;
        }
        
        file = malloc(strlen(dir) + strlen(entry->filename) + 2);
        strcpy(file, dir);
        strcat(file, "/");
        strcat(file, entry->filename);

        /* Convert Backslashes into front */
        ptr = file;
        while (*ptr)
        {
            if (*ptr == '\\')
                *ptr = '/';
            ptr++;
        }
        
        ptr = strrchr(file, '/');
        if (ptr != NULL)
        {
            *ptr = '\0';
            
            mkdir(file, S_IRUSR | S_IWUSR | S_IXUSR );
            
            if (ptr != NULL)
            {
                *ptr = '/';
            }
        }

        out = fopen(file, "w");
        if (out == NULL)
        {
            perror("Unable to open output file");
            exit(1);
        }
        
        ptr = read_pbo_entry(f, entry, &len);
        fwrite(ptr, len, 1, out);
        fclose(out);
        entry = entry->next;
    }
    close_pbo(pbo);
    
    return 0;
}
