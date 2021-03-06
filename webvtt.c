/* WebVTT parser
   Copyright 2011 Mozilla Foundation

   This Source Code Form is subject to the terms of the Mozilla
   Public License, v. 2.0. If a copy of the MPL was not distributed
   with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "webvtt.h"

#define BUFFER_SIZE 4096

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

struct webvtt_parser {
  int state;
  char *buffer;
  long offset, length;
};

webvtt_parser *
webvtt_parse_new(void)
{
  webvtt_parser *ctx = malloc(sizeof(*ctx));
  if (ctx) {
    ctx->state = 0;
    ctx->buffer = malloc(BUFFER_SIZE);
    if (ctx->buffer == NULL) {
      free(ctx);
      return NULL;
    }
    ctx->offset = 0;
    ctx->length = 0;
  }
  return ctx;
}

void
webvtt_parse_free(webvtt_parser *ctx)
{
  if (ctx) {
    ctx->state = 0;
    if (ctx->buffer) {
      free(ctx->buffer);
      ctx->buffer = NULL;
    }
    ctx->offset = 0;
    ctx->length = 0;
  }
}

int
webvtt_print_cue(FILE *out, webvtt_cue *cue)
{
  int err;
  int h, m, s, ms;
  long time;

  if (out == NULL || cue == NULL)
    return -1;

  time = cue->start;
  h = time/3600000;
  time %= 3600000;
  m = time/60000;
  time %= 60000;
  s = time/1000;
  ms = time%1000;
  err = fprintf(out, "%02d:%02d:%02d.%03d", h, m, s, ms);

  time = cue->end;
  h = time/3600000;
  time %= 3600000;
  m = time/60000;
  time %= 60000;
  s = time/1000;
  err = fprintf(out, " --> %02d:%02d:%02d.%03d\n", h, m, s, ms);

  err = fprintf(out, "%s\n\n", cue->text);

  return err;
}

webvtt_cue *
webvtt_parse_cue(webvtt_parser *ctx)
{
  webvtt_cue *cue = NULL;

  if (ctx == NULL)
    return NULL;
  if (ctx->buffer == NULL || ctx->length - ctx->offset < 24)
    return NULL;

  char *p = ctx->buffer + ctx->offset;
  while (p - ctx->buffer < ctx->length && isspace(*p))
    p++;

  int smin,ssec,smsec;
  int emin,esec,emsec;
  int items = sscanf(p, "%d:%d.%d --> %d:%d.%d",
                        &smin, &ssec, &smsec, &emin, &esec, &emsec);
  if (items < 6) {
    fprintf(stderr, "Couldn't parse cue timestamps\n");
    return NULL;
  }
  double start_time = smin*60 + ssec + smsec * 1e-3;
  double end_time = emin*60 + esec + emsec * 1e-3;

  while (p - ctx->buffer < ctx->length && *p != '\r' && *p != '\n')
    p++;
  p++;
  char *e = p;
  while (e - ctx->buffer + 4 < ctx->length) {
    if (!memcmp(e, "\n\n", 2))
      break;
    if (!memcmp(e, "\r\n\r\n", 4))
      break;
    if (!memcmp(e, "\r\r", 2))
      break;
    e++;
  }
  // TODO: handle last four bytes properly
  while (e - ctx->buffer < ctx->length && *e != '\n' && *e != '\r')
    e++;

  char *text = malloc(e - p + 1);
  if (text == NULL) {
    fprintf(stderr, "Couldn't allocate cue text buffer\n");
    return NULL;
  }
  cue = malloc(sizeof(*cue));
  if (cue == NULL) {
    fprintf(stderr, "Couldn't allocate cue structure\n");
    free(text);
    return NULL;
  }
  memcpy(text, p, e - p);
  text[e - p] = '\0';

  cue->start = start_time * 1e3;
  cue->end = end_time * 1e3;
  cue->text = text;
  cue->next = NULL;

  ctx->offset = e - ctx->buffer;

  return cue;
}

webvtt_cue *
webvtt_parse(webvtt_parser *ctx)
{
  webvtt_cue *cue = NULL;
  char *p = ctx->buffer;

  // Check for signature
  if (ctx->length < 6) {
    fprintf(stderr, "Too short. Not a webvtt file\n");
    return NULL;
  }
  if (p[0] == (char)0xef && p[1] == (char)0xbb && p[2] == (char)0xbf) {
    fprintf(stderr, "Byte order mark\n");
    p += 3;
    if (ctx->length < 9) {
      fprintf(stderr, "Too short. Not a webvtt file\n");
      return NULL;
    }
  }
  if (memcmp(p, "WEBVTT", 6)) {
    fprintf(stderr, "Bad magic. Not a webvtt file?\n");
    return NULL;
  }
  p += 6;
  fprintf(stderr, "Found signature\n");

  // skip whitespace
  while (p - ctx->buffer < ctx->length && isspace(*p))
    p++;
  ctx->offset = p - ctx->buffer;

  cue = webvtt_parse_cue(ctx);
  if (cue) {
    webvtt_cue *head = cue;
    webvtt_cue *next;
    do {
      next = webvtt_parse_cue(ctx);
      head->next = next;
      head = next;
    } while (next != NULL);
  }

  webvtt_cue *head = cue;
  while (head != NULL) {
    webvtt_print_cue(stderr, head);
    head = head->next;
  }

  return cue;
}

webvtt_cue *
webvtt_parse_buffer(webvtt_parser *ctx, char *buffer, long length)
{
  long bytes = MIN(length, BUFFER_SIZE - ctx->length);

  memcpy(ctx->buffer, buffer, bytes);
  ctx->length += bytes;

  return webvtt_parse(ctx);
}

webvtt_cue *
webvtt_parse_file(webvtt_parser *ctx, FILE *in)
{
  ctx->length = fread(ctx->buffer, 1, BUFFER_SIZE, in);
  ctx->offset = 0;

  if (ctx->length >= BUFFER_SIZE)
    fprintf(stderr, "WARNING: truncating input at %d bytes."
                    " This is a bug,\n", BUFFER_SIZE);

  return webvtt_parse(ctx);
}

webvtt_cue *
webvtt_parse_filename(webvtt_parser *ctx, const char *filename)
{
  FILE *in = fopen(filename, "r");
  webvtt_cue *cue = NULL;
  
  if (in) {
    cue = webvtt_parse_file(ctx, in);
    fclose(in);
  }
  
  return cue;
}
