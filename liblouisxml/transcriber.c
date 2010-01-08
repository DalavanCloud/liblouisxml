/* liblouisxml Braille Transcription Library

   This file may contain code borrowed from the Linux screenreader
   BRLTTY, copyright (C) 1999-2006 by
   the BRLTTY Team

   Copyright (C) 2004, 2005, 2006
   ViewPlus Technologies, Inc. www.viewplus.com
   and
   JJB Software, Inc. www.jjb-software.com
   All rights reserved

   This file is free software; you can redistribute it and/or modify it
   under the terms of the Lesser or Library GNU General Public License 
   as published by the
   Free Software Foundation; either version 3, or (at your option) any
   later version.

   This file is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
   Library GNU General Public License for more details.

   You should have received a copy of the Library GNU General Public 
   License along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.

   Maintained by John J. Boyer john.boyer@jjb-software.com
   */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "louisxml.h"

#define LETSIGN "\\_"
static StyleRecord *styleSpec;
/* Note that the following is an actual data area, not a pointer*/
static StyleRecord prevStyleSpec;
static StyleType *style;
static StyleType *prevStyle;
static int styleBody (void);

int
fineFormat (void)
{
  if (ud->text_length == 0 && ud->translated_length == 0)
    return 1;
  else
    {
      insert_translation (ud->mainBrailleTable);
      if (styleSpec == NULL)
	write_paragraph (para);
      else
	styleBody ();
      styleSpec->status = resumeBody;
    }
  return 1;
}

StyleType *
find_current_style (void)
{
  StyleRecord *sr = &ud->style_stack[ud->style_top];
  return sr->style;
}

static int doLeftJustify (void);
static widechar pageNumberString[32];
static int pageNumberLength;
static char *litHyphen = "-";
static char *compHyphen = "_&";
static char *blanks =
  "                                                                      ";
static int fillPage (void);
static int writeOutbuf (void);
static int insertCharacters (char *chars, int length);

int
start_document (void)
{
  if (ud->has_math)
    ud->mainBrailleTable = ud->mathtext_table_name;
  else
    ud->mainBrailleTable = ud->contracted_table_name;
  if (!lou_getTable (ud->mainBrailleTable))
    return 0;
  if (ud->has_contentsheader)
    ud->braille_page_number = 1;
  else
    ud->braille_page_number = ud->beginning_braille_page_number;
  ud->outlen_so_far = 0;
  styleSpec = &prevStyleSpec;
  style = prevStyle = lookup_style ("document");
  prevStyleSpec.style = prevStyle;
  if (ud->outFile && ud->output_encoding == utf16)
    {
      /*Little Endian indicator */
      fputc (0xff, ud->outFile);
      fputc (0xfe, ud->outFile);
    }
  switch (ud->format_for)
    {
    case textDevice:
      break;
    case browser:
      if (!insertCharacters
	  ("<html><head><title>HTML Document</title></head><body><pre>", 58))
	return 0;
      if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
	return 0;
      writeOutbuf ();
      break;
    default:
      break;
    }
  if (ud->contents && !ud->has_contentsheader)
    initialize_contents ();
  return 1;
}

int
end_document (void)
{
  if (ud->text_length != 0)
    insert_translation (ud->mainBrailleTable);
  if (ud->translated_length != 0)
    write_paragraph (para);
  if (ud->braille_pages)
    {
      fillPage ();
      writeOutbuf ();
    }
  if (ud->contents)
    make_contents ();
  switch (ud->format_for)
    {
    case textDevice:
      break;
    case browser:
      if (!insertCharacters ("</pre></body></html>", 20))
	return 0;
      if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
	return 0;
      writeOutbuf ();
      break;
    default:
      break;
    }
  return 1;
}

int
transcribe_text_string (void)
{
  int charsProcessed = 0;
  int charsInParagraph = 0;
  int ch;
  int pch = 0;
  unsigned char paragraphBuffer[BUFSIZE];
  StyleType *docStyle = lookup_style ("document");
  StyleType *paraStyle = lookup_style ("para");
  if (!start_document ())
    return 0;
  ud->input_encoding = ud->input_text_encoding;
  start_style (docStyle);
  while (1)
    {
      while (charsProcessed < ud->inlen)
	{
	  start_style (paraStyle);
	  ch = ud->inbuf[charsProcessed++];
	  if (ch == 0 || ch == 13)
	    continue;
	  if (ch == '\n' && pch == '\n')
	    break;
	  if (charsInParagraph == 0 && ch <= 32)
	    continue;
	  pch = ch;
	  if (ch == 10)
	    ch = ' ';
	  if (charsInParagraph >= MAX_LENGTH)
	    break;
	  paragraphBuffer[charsInParagraph++] = ch;
	}
      if (charsInParagraph == 0)
	break;
      ch = ud->inbuf[charsProcessed++];
      paragraphBuffer[charsInParagraph] = 0;
      if (!insert_utf8 (paragraphBuffer))
	return 0;
      if (!insert_translation (ud->mainBrailleTable))
	return 0;
      if (ch == 10)
	do_blankline ();
      end_style (paraStyle);
      charsInParagraph = 0;
      pch = 0;
      if (ch > 32)
	paragraphBuffer[charsInParagraph++] = ch;
    }
  ud->input_encoding = utf8;
  end_style (docStyle);
  end_document ();
  return 1;
}

int
transcribe_text_file (void)
{
  int charsInParagraph = 0;
  int ch;
  int pch = 0;
  unsigned char paragraphBuffer[BUFSIZE];
  widechar outbufx[BUFSIZE];
  int outlenx = MAX_LENGTH;
  StyleType *docStyle = lookup_style ("document");
  StyleType *paraStyle = lookup_style ("para");
  if (!start_document ())
    return 0;
  start_style (docStyle);
  ud->outbuf = outbufx;
  ud->outlen = outlenx;
  ud->input_encoding = ud->input_text_encoding;
  while (1)
    {
      start_style (paraStyle);
      while ((ch = fgetc (ud->inFile)) != EOF)
	{
	  if (ch == 0 || ch == 13)
	    continue;
	  if (pch == 10 && ch == 10)
	    break;
	  if (charsInParagraph == 0 && ch <= 32)
	    continue;
	  pch = ch;
	  if (ch < 32)
	    ch = ' ';
	  if (charsInParagraph >= MAX_LENGTH)
	    break;
	  paragraphBuffer[charsInParagraph++] = ch;
	}
      if (charsInParagraph == 0)
	break;
      ch = fgetc (ud->inFile);
      paragraphBuffer[charsInParagraph] = 0;
      if (!insert_utf8 (paragraphBuffer))
	return 0;
      if (!insert_translation (ud->mainBrailleTable))
	return 0;
      if (ch == 10)
	do_blankline ();
      end_style (paraStyle);
      charsInParagraph = 0;
      pch = 0;
      if (ch > 32)
	paragraphBuffer[charsInParagraph++] = ch;
    }
  ud->input_encoding = utf8;
  end_style (docStyle);
  end_document ();
  return 1;
}

#define MAXBYTES 7
static int first0Bit[MAXBYTES] = { 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0XFE };

static int
utf8ToWc (const unsigned char *utf8str, int *inSize, widechar *
	  utfwcstr, int *outSize)
{
  int in = 0;
  int out = 0;
  int lastInSize = 0;
  int lastOutSize = 0;
  unsigned int ch;
  int numBytes;
  unsigned int utf32;
  int k;
  while (in < *inSize)
    {
      ch = utf8str[in++] & 0xff;
      if (ch < 128 || ud->input_encoding == ascii8)
	{
	  utfwcstr[out++] = (widechar) ch;
	  if (out >= *outSize)
	    {
	      *inSize = in;
	      *outSize = out;
	      return 1;
	    }
	  continue;
	}
      lastInSize = in;
      lastOutSize = out;
      for (numBytes = MAXBYTES - 1; numBytes >= 0; numBytes--)
	if (ch >= first0Bit[numBytes])
	  break;
      utf32 = ch & (0XFF - first0Bit[numBytes]);
      for (k = 0; k < numBytes; k++)
	{
	  if (in >= *inSize)
	    break;
	  utf32 = (utf32 << 6) + (utf8str[in++] & 0x3f);
	}
      if (CHARSIZE == 2 && utf32 > 0xffff)
	utf32 = 0xffff;
      utfwcstr[out++] = (widechar) utf32;
      if (out >= *outSize)
	{
	  *inSize = lastInSize;
	  *outSize = lastOutSize;
	  return 1;
	}
    }
  *inSize = in;
  *outSize = out;
  return 1;
}

static unsigned char *
utfwcto8 (widechar utfwcChar)
{
  static unsigned char utf8Str[10];
  unsigned int utf8Bytes[MAXBYTES] = { 0, 0, 0, 0, 0, 0, 0 };
  int numBytes;
  int k;
  unsigned int utf32;
  if (utfwcChar < 128)
    {
      utf8Str[0] = utfwcChar;
      utf8Str[1] = 0;
      return utf8Str;
    }
  utf32 = utfwcChar;
  for (numBytes = 0; numBytes < MAXBYTES - 1; numBytes++)
    {
      utf8Bytes[numBytes] = utf32 & 0x3f;
      utf32 >>= 6;
      if (utf32 == 0)
	break;
    }
  utf8Str[0] = first0Bit[numBytes] | utf8Bytes[numBytes];
  numBytes--;
  k = 1;
  while (numBytes >= 0)
    utf8Str[k++] = utf8Bytes[numBytes--] | 0x80;
  utf8Str[k] = 0;
  return utf8Str;
}

static int
minimum (int x, int y)
{
  if (x <= y)
    return x;
  return y;
}

int
insert_utf8 (unsigned char *text)
{
  int length = strlen ((char *) text);
  int charsToDo = 0;
  int maxSize = 0;
  int charsDone = length;
  int outSize = MAX_LENGTH - ud->text_length;
  utf8ToWc (text, &charsDone, &ud->text_buffer[ud->text_length], &outSize);
  ud->text_length += outSize;
  while (charsDone < length)
    {
      /*Handle buffer overflow */
      StyleType *style = find_current_style ();
      char *table;
      if (style == NULL)
	style = lookup_style ("para");
      switch (style->action)
	{
	case code:
	  table = ud->compbrl_table_name;
	  memset (ud->typeform, computer_braille, ud->text_length);
	  break;
	default:
	  table = ud->mainBrailleTable;
	  break;
	}
      if (!insert_translation (table))
	return 0;
      if (!write_paragraph (style->action))
	return 0;
      charsToDo = minimum (MAX_LENGTH, length - charsDone);
      while (text[charsDone + charsToDo] > 32)
	charsToDo--;
      if (charsToDo <= 0)
	charsToDo = minimum (MAX_LENGTH, length - charsDone);
      maxSize = MAX_LENGTH;
      utf8ToWc (&text[charsDone], &charsToDo, &ud->text_buffer[0], &maxSize);
      charsDone += charsToDo;
    }
  return length;
}

int
insert_utfwc (widechar * text, int length)
{
  if (length < 0)
    return 0;
  if ((ud->text_length + length) > MAX_LENGTH)
    return 0;
  memcpy (&ud->text_buffer[ud->text_length], text, CHARSIZE * length);
  ud->text_length += length;
  return length;
}

int
insert_translation (char *table)
{
  int translationLength;
  int translatedLength;
  int k;
  if (table[0] == 0)
    {
      memset (ud->typeform, 0, sizeof (ud->typeform));
      ud->text_length = 0;
      return 0;
    }
  if (ud->text_length == 0)
    return 1;
  for (k = 0; k < ud->text_length && ud->text_buffer[k] <= 32; k++);
  if (k == ud->text_length)
    {
      ud->text_length = 0;
      return 1;
    }
  if (styleSpec != NULL && styleSpec->status == resumeBody)
    styleSpec->status = bodyInterrupted;
  if (ud->translated_length > 0 && ud->translated_length <
      MAX_TRANS_LENGTH &&
      ud->translated_buffer[ud->translated_length - 1] > 32)
    ud->translated_buffer[ud->translated_length++] = 32;
  translatedLength = MAX_TRANS_LENGTH - ud->translated_length;
  translationLength = ud->text_length;
  ud->text_buffer[ud->text_length++] = 32;
  ud->text_buffer[ud->text_length++] = 32;
  k = lou_translateString (table,
			   &ud->text_buffer[0], &translationLength,
			   &ud->translated_buffer[ud->translated_length],
			   &translatedLength, (char *)
			   &ud->typeform[0], NULL, 0);
  memset (ud->typeform, 0, sizeof (ud->typeform));
  ud->text_length = 0;
  if (!k)
    {
      table[0] = 0;
      return 0;
    }
  if ((ud->translated_length + translatedLength) < MAX_TRANS_LENGTH)
    {
      ud->translated_length += translatedLength;
    }
  else
    {
      ud->translated_length = MAX_TRANS_LENGTH;
      if (!write_paragraph (para))
	return 0;
    }
  return 1;
}

static int cellsWritten;
static int
insertCharacters (char *chars, int length)
{
/* Put chars in outbuf, checking for overflow.*/
  int k;
  if (chars == NULL || length < 0)
    return 0;
  if (length == 0)
    return 1;
  if ((ud->outlen_so_far + length) >= ud->outlen)
    return 0;
  for (k = 0; k < length; k++)
    ud->outbuf[ud->outlen_so_far++] = (widechar) chars[k];
  cellsWritten += length;
  return 1;
}

static int
insertDubChars (char *chars, int length)
{
/* Put chars in outbuf, checking for overflow.*/
  int k;
  if (chars == NULL || length < 0)
    return 0;
  while (length > 0 && chars[length - 1] == ' ')
    length--;
  cellsWritten += length;
  if (length == 0)
    return 1;
  if ((ud->outlen_so_far + length) >= ud->outlen)
    return 0;
  switch (ud->format_for)
    {
    case textDevice:
      for (k = 0; k < length; k++)
	ud->outbuf[ud->outlen_so_far++] = (widechar) chars[k];
      break;
    case browser:
      for (k = 0; k < length; k++)
	{
	  if (chars[k] == '<')
	    {
	      if (!insertCharacters ("&lt;", 4))
		return 0;
	    }
	  else if (chars[k] == '&')
	    {
	      if (!insertCharacters ("&amp;", 5))
		return 0;
	    }
	  else
	    ud->outbuf[ud->outlen_so_far++] = (widechar) chars[k];
	}
      break;
    default:
      break;
    }
  return 1;
}

static int
insertWidechars (widechar * chars, int length)
{
/* Put chars in outbuf, checking for overflow.*/
  int k;
  if (chars == NULL || length < 0)
    return 0;
  while (length > 0 && chars[length - 1] == ' ')
    length--;
  cellsWritten += length;
  if (length == 0)
    return 1;
  if ((ud->outlen_so_far + length) >= ud->outlen)
    return 0;
  switch (ud->format_for)
    {
    case textDevice:
      memcpy (&ud->outbuf[ud->outlen_so_far], chars, length * CHARSIZE);
      ud->outlen_so_far += length;
      break;
    case browser:
      for (k = 0; k < length; k++)
	{
	  if (chars[k] == '<')
	    {
	      if (!insertCharacters ("&lt;", 4))
		return 0;
	    }
	  else if (chars[k] == '&')
	    {
	      if (!insertCharacters ("&amp;", 5))
		return 0;
	    }
	  else
	    ud->outbuf[ud->outlen_so_far++] = chars[k];
	}
      break;
    default:
      break;
    }
  return 1;
}

static int
doInterline (void)
{
  int k;
  int translationLength;
  widechar *translationBuffer;
  widechar translatedBuffer[MAXNAMELEN];
  int translatedLength = MAXNAMELEN;
  char *table;
  if (ud->outlen_so_far == 0 || ud->outbuf[ud->outlen_so_far - 1] < 32)
    {
      if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
	return 0;
      if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
	return 0;
      return 1;
    }
  for (k = ud->outlen_so_far - 1; k > 0 && ud->outbuf[k] >= 32; k--);
  if (k > 0)
    k++;
  translationBuffer = &ud->outbuf[k];
  translationLength = ud->outlen_so_far - k;
  if (*ud->interline_back_table_name)
    table = ud->interline_back_table_name;
  else
    table = ud->mainBrailleTable;
  if (!lou_backTranslateString (table, translationBuffer,
				&translationLength, translatedBuffer,
				&translatedLength, NULL, NULL, 0))
    return 0;
  for (k = 0; k < translatedLength; k++)
    if (translatedBuffer[k] == 0xa0 || (translatedBuffer[k] < 32 &&
					translatedBuffer[k] != 9))
      translatedBuffer[k] = 32;
  if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
    return 0;
  if (!insertWidechars (translatedBuffer, translatedLength))
    return 0;
  if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
    return 0;
  return 1;
}

static int startLine (void);
static int finishLine (void);

static int
makeBlankLines (int number, int beforeAfter)
{
  int availableCells;
  int k;
  if (number == 0)
    return 1;
  if (ud->braille_pages)
    {
      if (beforeAfter == 0 && (ud->lines_on_page == 0 ||
			       prevStyle->lines_after > 0
			       || prevStyle->action == document))
	return 1;
      else if (beforeAfter == 1 && (ud->lines_per_page - ud->lines_on_page -
				    number) < 2)
	return 1;
    }
  else
    {
      if (beforeAfter == 0 && (prevStyle->lines_after || prevStyle->action ==
			       document))
	return 1;
    }
  for (k = 0; k < number; k++)
    {
      availableCells = startLine ();
      if (!finishLine ())
	return 0;
    }
  return 1;
}

static int
fillPage (void)
{
  if (!ud->braille_pages)
    return 1;
  if (!makeBlankLines (ud->lines_per_page - ud->lines_on_page, 2))
    return 0;
  writeOutbuf ();
  return 1;
}

static int
makePageSeparator (xmlChar * printPageNumber, int length)
{
  int k;
  int kk;
  widechar translationBuffer[MAXNUMLEN];
  int translationLength = MAXNUMLEN - 1;
  widechar translatedBuffer[MAXNUMLEN];
  int translatedLength = MAXNUMLEN;
  widechar separatorLine[128];
  char setup[MAXNAMELEN];
  if (!ud->print_pages || !*printPageNumber)
    return 1;
  if (ud->braille_pages && ud->lines_on_page >= (ud->lines_per_page - 2))
    fillPage ();
  strcpy (setup, " ");
  if (!(printPageNumber[0] >= '0' && printPageNumber[0] <= '9'))
    strcat (setup, LETSIGN);
  strcat (setup, printPageNumber);
  length = strlen (setup);
  utf8ToWc (setup, &length, &translationBuffer[0], &translationLength);
  if (!lou_translateString (ud->mainBrailleTable, translationBuffer,
			    &translationLength, translatedBuffer,
			    &translatedLength, NULL, NULL, 0))
    return 0;
  if (ud->braille_pages && ud->lines_on_page == 0)
    return 1;
  else
    translatedBuffer[0] = '-';
  for (k = 0; k < (ud->cells_per_line - translatedLength); k++)
    separatorLine[k] = '-';
  kk = 0;
  for (; k < ud->cells_per_line; k++)
    separatorLine[k] = translatedBuffer[kk++];
  if (!insertWidechars (separatorLine, ud->cells_per_line))
    return 0;
  if (ud->interline)
    {
      if (!doInterline ())
	return 0;
    }
  else if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
    return 0;
  ud->lines_on_page++;
  translatedBuffer[0] = 'a';
  for (k = 0; k < translatedLength; k++)
    ud->print_page_number[k] = translatedBuffer[k];
  ud->print_page_number[k] = 0;
  writeOutbuf ();
  return 1;
}

void
insert_text (xmlNode * node)
{
  int length = strlen ((char *) node->content);
  switch (ud->stack[ud->top])
    {
    case notranslate:
      insert_translation (ud->mainBrailleTable);
      insert_utf8 (node->content);
      if ((ud->translated_length + ud->text_length) > MAX_TRANS_LENGTH)
	ud->text_length = MAX_TRANS_LENGTH - ud->translated_length;
      memcpy (&ud->translated_buffer[ud->translated_length], ud->text_buffer,
	      ud->text_length * CHARSIZE);
      ud->translated_length += ud->text_length;
      ud->text_length = 0;
      return;
    case pagenum:
      if (!ud->print_pages)
	return;
      fineFormat ();
      makePageSeparator (node->content, length);
      return;
    case italicx:
      memset (&ud->typeform[ud->text_length], italic, length);
      break;
    case boldx:
      memset (&ud->typeform[ud->text_length], bold, length);
      break;
    case compbrl:
      memset (&ud->typeform[ud->text_length], computer_braille, length);
      break;
    default:
      break;
    }
  insert_utf8 (node->content);
}

static char *makeRomanNumber (int n);

static int
getBraillePageString (void)
{
  int k;
  char brlPageString[40];
  widechar translationBuffer[MAXNUMLEN];
  int translationLength;
  int translatedLength = MAXNUMLEN;
  switch (ud->brl_page_num_format)
    {
    case none:
      pageNumberLength = 0;
      return 1;
    default:
    case normal:
      translationLength =
	sprintf (brlPageString, "%d", ud->braille_page_number);
      break;
    case p:
      translationLength = sprintf (brlPageString, "p%d",
				   ud->braille_page_number);
      break;
    case roman:
      strcpy (brlPageString, LETSIGN);
      strcat (brlPageString, makeRomanNumber (ud->braille_page_number));
      translationLength = strlen (brlPageString);
      break;
    }
  for (k = 0; k < translationLength; k++)
    translationBuffer[k] = brlPageString[k];
  if (!lou_translateString (ud->mainBrailleTable, translationBuffer,
			    &translationLength, ud->braille_page_string,
			    &translatedLength, NULL, NULL, 0))
    return 0;
  ud->braille_page_string[translatedLength] = 0;
  for (pageNumberLength = 0; pageNumberLength < 3; pageNumberLength++)
    pageNumberString[pageNumberLength] = ' ';
  for (k = 0; k < translatedLength; k++)
    pageNumberString[pageNumberLength++] = ud->braille_page_string[k];
  return 1;
}

static char *
makeRomanNumber (int n)
{
  static char romNum[40];
  static const char *hundreds[] = {
    "",
    "c",
    "cc",
    "ccc",
    "cd",
    "d",
    "dc",
    "dcc",
    "dccc",
    "cm",
    "m"
  };
  static const char *tens[] = {
    "",
    "x",
    "xx",
    "xxx",
    "xl",
    "l",
    "lx",
    "lxx",
    "lxxx",
    "xc"
  };
  static const char *units[] = {
    "",
    "i",
    "ii",
    "iii",
    "iv",
    "v",
    "vi",
    "vii",
    "viii",
    "ix"
  };
  if (n <= 0 || n > 1000)
    return NULL;
  romNum[0] = 0;
  strcat (romNum, hundreds[n / 100]);
  strcat (romNum, tens[(n / 10) % 10]);
  strcat (romNum, units[n % 10]);
  return romNum;
}

static int
getPageNumber (void)
{
  int choice = 0;
  int k;
  if ((ud->lines_on_page == 1 && ud->print_page_number_at) ||
      (ud->lines_on_page == ud->lines_per_page && !ud->print_page_number_at))
    choice = 1;
  else if ((ud->lines_on_page == ud->lines_per_page &&
	    ud->braille_page_number_at) || (ud->lines_on_page == 1 &&
					    !ud->braille_page_number_at))
    choice = 2;
  if (choice == 2 && ud->interpoint && !(ud->braille_page_number & 1))
    choice = 0;
  switch (choice)
    {
    case 0:
      pageNumberLength = 0;
      return 1;
    case 1:
      if (ud->print_page_number[0] == '_' || !ud->print_pages)
	{
	  pageNumberLength = 0;
	  return 1;
	}
      for (pageNumberLength = 0; pageNumberLength < 3; pageNumberLength++)
	pageNumberString[pageNumberLength] = ' ';
      for (k = 0; ud->print_page_number[k]; k++)
	pageNumberString[pageNumberLength++] = ud->print_page_number[k];
      if (ud->print_page_number[0] == 'z')
	ud->print_page_number[0] = '_';
      if (ud->print_page_number[0] == ' ')
	ud->print_page_number[0] = 'a';
      else
	ud->print_page_number[0]++;
      return 1;
    case 2:
      getBraillePageString ();
      return 1;
    }
  return 0;
}

static int
startLine (void)
{
  int availableCells = 0;
  while (availableCells == 0)
    {
      cellsWritten = 0;
      if (ud->braille_pages)
	ud->lines_on_page++;
      else
	return ud->cells_per_line;
      getPageNumber ();
      if (ud->lines_on_page == 1)
	{
	  if (ud->running_head_length > 0 || (style->skip_number_lines &&
					      pageNumberLength > 0))
	    {
	      finishLine ();
	      continue;
	    }
	  availableCells = ud->cells_per_line - pageNumberLength;
	}
      else if (ud->lines_on_page == ud->lines_per_page)
	{
	  if (ud->footer_length > 0 ||
	      (style->skip_number_lines && pageNumberLength > 0))
	    {
	      finishLine ();
	      continue;
	    }
	  availableCells = ud->cells_per_line - pageNumberLength;
	}
      else
	availableCells = ud->cells_per_line;
    }
  return availableCells;
}

static int
finishLine (void)
{
  int cellsToWrite = 0;
  int leaveBlank;
  for (leaveBlank = -1; leaveBlank < ud->line_spacing; leaveBlank++)
    {
      if (leaveBlank != -1)
	startLine ();
      if (ud->braille_pages)
	{
	  if (cellsWritten > 0 && pageNumberLength > 0)
	    {
	      cellsToWrite =
		ud->cells_per_line - pageNumberLength - cellsWritten;
	      if (!insertCharacters (blanks, cellsToWrite))
		return 0;
	      if (!insertWidechars (pageNumberString, pageNumberLength))
		return 0;
	    }
	  else if (ud->lines_on_page == 1)
	    {
	      if (ud->running_head_length > 0)
		{
		  cellsToWrite =
		    minimum (ud->running_head_length,
			     ud->cells_per_line - pageNumberLength);
		  if (!insertWidechars (ud->running_head, cellsToWrite))
		    return 0;
		  if (pageNumberLength)
		    {
		      cellsToWrite =
			ud->cells_per_line - pageNumberLength - cellsToWrite;
		      if (!insertCharacters (blanks, cellsToWrite))
			return 0;
		      if (!insertWidechars
			  (pageNumberString, pageNumberLength))
			return 0;
		    }
		}
	      else
		{
		  if (pageNumberLength)
		    {
		      cellsToWrite = ud->cells_per_line - pageNumberLength;
		      if (!insertCharacters (blanks, cellsToWrite))
			return 0;
		      if (!insertWidechars
			  (pageNumberString, pageNumberLength))
			return 0;
		    }
		}
	    }
	  else if (ud->lines_on_page == ud->lines_per_page)
	    {
	      if (ud->footer_length > 0)
		{
		  cellsToWrite =
		    minimum (ud->footer_length,
			     ud->cells_per_line - pageNumberLength);
		  if (!insertWidechars (ud->footer, cellsToWrite))
		    return 0;
		  if (pageNumberLength)
		    {
		      cellsToWrite =
			ud->cells_per_line - pageNumberLength - cellsToWrite;
		      if (!insertCharacters (blanks, cellsToWrite))
			return 0;
		      if (!insertWidechars
			  (pageNumberString, pageNumberLength))
			return 0;
		    }
		}
	      else
		{
		  if (pageNumberLength)
		    {
		      cellsToWrite = ud->cells_per_line - pageNumberLength;
		      if (!insertCharacters (blanks, cellsToWrite))
			return 0;
		      if (!insertWidechars
			  (pageNumberString, pageNumberLength))
			return 0;
		    }
		}
	    }
	}
      if (ud->interline)
	{
	  if (!doInterline ())
	    return 0;
	}
      else if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
	return 0;
      if (ud->braille_pages && ud->lines_on_page == ud->lines_per_page)
	{
	  if (!insertCharacters (ud->pageEnd, strlen (ud->pageEnd)))
	    return 0;
	  ud->lines_on_page = 0;
	  ud->braille_page_number++;
	}
    }
  return 1;
}

static int
makeBlankPage (void)
{
  if (!ud->braille_pages)
    return 1;
  if (!makeBlankLines (ud->lines_per_page, 2))
    return 0;
  return 1;
}

static int
writeOutbuf (void)
{
  int k;
  unsigned char *utf8Str;
  if (ud->outlen_so_far == 0 || ud->outFile == NULL)
    return 1;			/*output stays in ud->outbuf */
  switch (ud->output_encoding)
    {
    case utf8:
      for (k = 0; k < ud->outlen_so_far; k++)
	{
	  utf8Str = utfwcto8 (ud->outbuf[k]);
	  fwrite (utf8Str, strlen ((char *) utf8Str), 1, ud->outFile);
	}
      break;
    case utf16:
      for (k = 0; k < ud->outlen_so_far; k++)
	{
	  unsigned short uc16 = (unsigned short) ud->outbuf[k];
	  fwrite (&uc16, 1, sizeof (uc16), ud->outFile);
	}
      break;
    case utf32:
      for (k = 0; k < ud->outlen_so_far; k++)
	{
	  unsigned int uc32 = (unsigned int) ud->outbuf[k];
	  fwrite (&uc32, 1, sizeof (uc32), ud->outFile);
	}
      break;
    case ascii8:
      for (k = 0; k < ud->outlen_so_far; k++)
	fputc ((char) ud->outbuf[k], ud->outFile);
      break;
    default:
      break;
    }
  ud->outlen_so_far = 0;
  return 1;
}

static widechar *translatedBuffer;
static int translationLength;
static int translatedLength;

static int
hyphenatex (int lastBlank, int lineEnd)
{
  char hyphens[MAXNAMELEN];
  int k;
  int wordStart = lastBlank + 1;
  int wordLength;
  int breakAt = 0;
  int hyphenFound = 0;
  if ((translatedLength - wordStart) < 12)
    return 0;
  for (wordLength = wordStart; wordLength < translatedLength; wordLength++)
    if (translatedBuffer[wordLength] == ' ')
      break;
  wordLength -= wordStart;
  if (wordLength < 5 || wordLength > ud->cells_per_line)
    return 0;
  for (k = wordLength - 2; k >= 0; k--)
    if ((wordStart + k) < lineEnd && translatedBuffer[wordStart + k] ==
	*litHyphen && !hyphenFound)
      {
	hyphens[k + 1] = '1';
	hyphenFound = 1;
      }
    else
      hyphens[k + 1] = '0';
  hyphens[wordLength] = 0;
  if (!hyphenFound)
    {
      if (!lou_hyphenate (ud->mainBrailleTable,
			  &translatedBuffer[wordStart], wordLength,
			  hyphens, 1))
	return 0;
    }
  for (k = strlen (hyphens) - 2; k > 0; k--)
    {
      breakAt = wordStart + k;
      if (hyphens[k] == '1' && breakAt < lineEnd)
	break;
    }
  if (k < 2)
    return 0;
  return breakAt;
}

#define escapeChar 0x1b

static int
doAlignColumns ()
{
#define MAXCOLS 100
#define MAXROWSIZE 400
#define COLSPACING 2
  int numRows = 0;
  int rowNum = 0;
  int numCols = 0;
  int colNum = 0;
  int colLength = 0;
  int rowLength;
  int colSize[MAXCOLS];
  widechar rowBuf[MAXROWSIZE];
  int bufPos;
  int k;
  unsigned int ch;
  int rowEnd = 0;
  for (bufPos = 0; bufPos < translatedLength; bufPos++)
    if (translatedBuffer[bufPos] == escapeChar)
      break;
  if (bufPos >= translatedLength)
    {
      doLeftJustify ();
      return 1;
    }
  for (k = 0; k < MAXCOLS; k++)
    colSize[k] = 0;

  /*Calculate number of columns and column sizes */
  while (bufPos < translatedLength)
    {
      ch = translatedBuffer[bufPos++];
      if (ch == escapeChar)
	{
	  unsigned int nch = translatedBuffer[bufPos];
	  if (nch == 'r')	/*End of row */
	    {
	      numRows++;
	      if (rowEnd == 0)
		rowEnd = colLength;
	      colLength = 0;
	      colNum = 0;
	      bufPos++;
	    }
	  else if (nch == 'c')
	    {
	      if (numRows == 0)
		numCols++;
	      if (colSize[colNum] < colLength)
		colSize[colNum] = colLength;
	      colNum++;
	      colLength = 0;
	      bufPos++;
	    }
	  else if (nch == 'e')
	    break;
	}
      else
	colLength++;
    }
  colSize[numCols - 1] += rowEnd;
  if (style->format == alignColumnsLeft)
    {
      /*Calculate starting points of columns in output */
      int colStart = 0;
      for (colNum = 0; colNum < numCols; colNum++)
	{
	  k = colSize[colNum];
	  colSize[colNum] = colStart;
	  colStart += k;
	  if (colNum != (numCols - 1))
	    colStart += COLSPACING;
	}
    }
  else
    {
      /*Calculate ending points of columns in output */
      int colEnd = colSize[0];
      for (colNum = 1; colNum < numCols; colNum++)
	{
	  colEnd += colSize[colNum] + COLSPACING;
	  colSize[colNum] = colEnd;
	}
    }

/*Now output the stuff.*/
  if ((ud->lines_per_page - ud->lines_on_page) < numRows)
    fillPage ();
  bufPos = 0;
  for (rowNum = 0; rowNum < numRows; rowNum++)
    {
      int charactersWritten = 0;
      int cellsToWrite = 0;
      int availableCells = 0;
      rowLength = 0;
      if (style->format == alignColumnsLeft)
	{
	  for (colNum = 0; colNum < numCols; colNum++)
	    {
	      while (rowLength < MAXROWSIZE
		     && translatedBuffer[bufPos] != escapeChar)
		rowBuf[rowLength++] = translatedBuffer[bufPos++];
	      bufPos += 2;
	      if (colNum < (numCols - 1))
		{
		  while (rowLength < MAXROWSIZE && rowLength <
			 colSize[colNum + 1])
		    rowBuf[rowLength++] = ' ';
		}
	      else
		{
		  while (rowLength < MAXROWSIZE
			 && translatedBuffer[bufPos] != escapeChar)
		    rowBuf[rowLength++] = translatedBuffer[bufPos++];
		  bufPos += 2;	/*actual end of row */
		}
	    }
	}
      else
	{
	  int prevBufPos = bufPos;
	  int prevCol = 0;
	  for (colNum = 0; colNum < numCols; colNum++)
	    {
	      while (translatedBuffer[bufPos] != escapeChar)
		bufPos++;
	      for (k = bufPos - 1; k >= prevBufPos; k--)
		rowBuf[k + prevCol] = translatedBuffer[k];
	      for (; k >= prevCol; k--)
		rowBuf[k + prevCol] = ' ';
	      prevBufPos = bufPos + 2;
	      prevCol = colSize[colNum];
	      rowLength += colSize[colNum];
	      if (rowLength > MAXROWSIZE)
		break;
	    }
	  while (rowLength < MAXROWSIZE && translatedBuffer[bufPos] !=
		 escapeChar)
	    rowBuf[rowLength++] = translatedBuffer[bufPos++];
	  bufPos += 2;
	}
      while (charactersWritten < rowLength)
	{
	  int rowTooLong = 0;
	  availableCells = startLine ();
	  if ((charactersWritten + availableCells) >= rowLength)
	    cellsToWrite = rowLength - charactersWritten;
	  else
	    {
	      for (cellsToWrite = availableCells; cellsToWrite > 0;
		   cellsToWrite--)
		if (rowBuf[charactersWritten + cellsToWrite] == ' ')
		  break;
	      if (cellsToWrite == 0)
		{
		  cellsToWrite = availableCells - 1;
		  rowTooLong = 1;
		}
	    }
	  while (rowBuf[charactersWritten + cellsToWrite] == ' ')
	    cellsToWrite--;
	  if (cellsToWrite == 0)
	    break;
	  for (k = charactersWritten;
	       k < (charactersWritten + cellsToWrite); k++)
	    if (rowBuf[k] == 0xa0)	/*unbreakable space */
	      rowBuf[k] = 0x20;	/*space */
	  if (!insertWidechars (&rowBuf[charactersWritten], cellsToWrite))
	    return 0;
	  charactersWritten += cellsToWrite;
	  if (rowTooLong)
	    {
	      if (!insertDubChars (litHyphen, strlen (litHyphen)))
		return 0;
	    }
	  finishLine ();
	}
    }
  return 1;
}

static int
doListColumns (void)
{
  widechar *thisRow;
  int rowLength;
  int bufPos;
  int prevPos = 0;
  for (bufPos = 0; bufPos < translatedLength; bufPos++)
    if (translatedBuffer[bufPos] == escapeChar)
      break;
  if (bufPos >= translatedLength)
    {
      doLeftJustify ();
      return 1;
    }
  for (; bufPos < translatedLength; bufPos++)
    {
      if (translatedBuffer[bufPos] == escapeChar &&
	  translatedBuffer[bufPos + 1] == escapeChar)
	{
	  int charactersWritten = 0;
	  int cellsToWrite = 0;
	  int availableCells = 0;
	  int k;
	  thisRow = &translatedBuffer[prevPos];
	  rowLength = bufPos - prevPos - 1;
	  prevPos = bufPos + 2;
	  while (charactersWritten < rowLength)
	    {
	      int wordTooLong = 0;
	      int breakAt = 0;
	      int leadingBlanks = 0;
	      availableCells = startLine ();
	      if (styleSpec->status == startBody)
		{
		  if (style->first_line_indent < 0)
		    leadingBlanks = 0;
		  else
		    leadingBlanks =
		      style->left_margin + style->first_line_indent;
		  styleSpec->status = resumeBody;
		}
	      else
		leadingBlanks = style->left_margin;
	      if (!insertCharacters (blanks, leadingBlanks))
		return 0;
	      availableCells -= leadingBlanks;
	      if ((charactersWritten + availableCells) >= rowLength)
		cellsToWrite = rowLength - charactersWritten;
	      else
		{
		  for (cellsToWrite = availableCells; cellsToWrite > 0;
		       cellsToWrite--)
		    if (thisRow[charactersWritten + cellsToWrite] == ' ')
		      break;
		  if (cellsToWrite == 0)
		    {
		      cellsToWrite = availableCells - 1;
		      wordTooLong = 1;
		    }
		  else
		    {
		      if (ud->hyphenate)
			breakAt =
			  hyphenatex (charactersWritten + cellsToWrite,
				      charactersWritten + availableCells);
		      if (breakAt)
			cellsToWrite = breakAt - charactersWritten;
		    }
		}
	      for (k = charactersWritten;
		   k < (charactersWritten + cellsToWrite); k++)
		if (thisRow[k] == 0xa0)	/*unbreakable space */
		  thisRow[k] = 0x20;	/*space */
	      if (!insertWidechars
		  (&thisRow[charactersWritten], cellsToWrite))
		return 0;
	      charactersWritten += cellsToWrite;
	      if (thisRow[charactersWritten] == ' ')
		charactersWritten++;
	      if ((breakAt && thisRow[breakAt - 1] != *litHyphen)
		  || wordTooLong)
		{
		  if (!insertDubChars (litHyphen, strlen (litHyphen)))
		    return 0;
		}
	      finishLine ();
	    }
	}
      else if (translatedBuffer[bufPos - 1] !=
	       escapeChar && translatedBuffer[bufPos] == escapeChar)
	translatedBuffer[bufPos] = ' ';
    }
  return 1;
}

static int
doListLines (void)
{
  widechar *thisLine;
  int lineLength;
  int bufPos;
  int prevPos = 0;
  for (bufPos = 0; bufPos < translatedLength; bufPos++)
    if (translatedBuffer[bufPos] == escapeChar)
      break;
  if (bufPos >= translatedLength)
    {
      doLeftJustify ();
      return 1;
    }
  for (; bufPos < translatedLength; bufPos++)
    if (translatedBuffer[bufPos] == escapeChar && translatedBuffer[bufPos + 1]
	== escapeChar)
      {
	int charactersWritten = 0;
	int cellsToWrite = 0;
	int availableCells = 0;
	int k;
	thisLine = &translatedBuffer[prevPos];
	lineLength = bufPos - prevPos - 1;
	prevPos = bufPos + 2;
	while (charactersWritten < lineLength)
	  {
	    int wordTooLong = 0;
	    int breakAt = 0;
	    int leadingBlanks = 0;
	    availableCells = startLine ();
	    if (styleSpec->status == startBody)
	      {
		if (style->first_line_indent < 0)
		  leadingBlanks = 0;
		else
		  leadingBlanks =
		    style->left_margin + style->first_line_indent;
		styleSpec->status = resumeBody;
	      }
	    else
	      leadingBlanks = style->left_margin;
	    if (!insertCharacters (blanks, leadingBlanks))
	      return 0;
	    availableCells -= leadingBlanks;
	    if ((charactersWritten + availableCells) >= lineLength)
	      cellsToWrite = lineLength - charactersWritten;
	    else
	      {
		for (cellsToWrite = availableCells; cellsToWrite > 0;
		     cellsToWrite--)
		  if (thisLine[charactersWritten + cellsToWrite] == ' ')
		    break;
		if (cellsToWrite == 0)
		  {
		    cellsToWrite = availableCells - 1;
		    wordTooLong = 1;
		  }
		else
		  {
		    if (ud->hyphenate)
		      breakAt =
			hyphenatex (charactersWritten + cellsToWrite,
				    charactersWritten + availableCells);
		    if (breakAt)
		      cellsToWrite = breakAt - charactersWritten;
		  }
	      }
	    for (k = charactersWritten;
		 k < (charactersWritten + cellsToWrite); k++)
	      if (thisLine[k] == 0xa0)	/*unbreakable space */
		thisLine[k] = 0x20;	/*space */
	    if (!insertWidechars (&thisLine[charactersWritten], cellsToWrite))
	      return 0;
	    charactersWritten += cellsToWrite;
	    if (thisLine[charactersWritten] == ' ')
	      charactersWritten++;
	    if ((breakAt && thisLine[breakAt - 1] != *litHyphen)
		|| wordTooLong)
	      {
		if (!insertDubChars (litHyphen, strlen (litHyphen)))
		  return 0;
	      }
	    finishLine ();
	  }
      }
  return 1;
}

static int
doComputerCode (void)
{
  int charactersWritten = 0;
  int cellsToWrite = 0;
  int availableCells = 0;
  int k;
  while (translatedBuffer[charactersWritten] == 0x0a)
    charactersWritten++;
  while (charactersWritten < translatedLength)
    {
      int lineTooLong = 0;
      availableCells = startLine ();
      for (cellsToWrite = 0; cellsToWrite < availableCells; cellsToWrite++)
	if ((charactersWritten + cellsToWrite) >= translatedLength
	    || translatedBuffer[charactersWritten + cellsToWrite] == 0x0a)
	  break;
      if ((charactersWritten + cellsToWrite) > translatedLength)
	cellsToWrite--;
      if (cellsToWrite <= 0 && translatedBuffer[charactersWritten] != 0x0a)
	break;
      if (cellsToWrite == availableCells &&
	  translatedBuffer[charactersWritten + cellsToWrite] != 0x0a)
	{
	  cellsToWrite = availableCells - strlen (compHyphen);
	  lineTooLong = 1;
	}
      if (translatedBuffer[charactersWritten + cellsToWrite] == 0x0a)
	translatedBuffer[charactersWritten + cellsToWrite] = ' ';
      for (k = charactersWritten; k < (charactersWritten + cellsToWrite); k++)
	if (translatedBuffer[k] == 0xa0)	/*unbreakable space */
	  translatedBuffer[k] = 0x20;	/*space */
      if (!insertWidechars
	  (&translatedBuffer[charactersWritten], cellsToWrite))
	return 0;
      charactersWritten += cellsToWrite;
      if (translatedBuffer[charactersWritten] == ' ')
	charactersWritten++;
      if (lineTooLong)
	{
	  if (!insertDubChars (compHyphen, strlen (compHyphen)))
	    return 0;
	}
      finishLine ();
    }
  return 1;
}

static int
doLeftJustify (void)
{
  int charactersWritten = 0;
  int cellsToWrite = 0;
  int availableCells = 0;
  int k;
  while (charactersWritten < translatedLength)
    {
      int wordTooLong = 0;
      int breakAt = 0;
      int leadingBlanks = 0;
      availableCells = startLine ();
      if (styleSpec->status == startBody)
	{
	  leadingBlanks = style->left_margin + style->first_line_indent;
	  styleSpec->status = resumeBody;
	}
      else
	leadingBlanks = style->left_margin;
      if (!insertCharacters (blanks, leadingBlanks))
	return 0;
      availableCells -= leadingBlanks;
      if ((charactersWritten + availableCells) >= translatedLength)
	cellsToWrite = translatedLength - charactersWritten;
      else
	{
	  for (cellsToWrite = availableCells; cellsToWrite > 0;
	       cellsToWrite--)
	    if (translatedBuffer[charactersWritten + cellsToWrite] == ' ')
	      break;
	  if (cellsToWrite == 0)
	    {
	      cellsToWrite = availableCells - 1;
	      wordTooLong = 1;
	    }
	  else
	    {
	      if (ud->hyphenate)
		breakAt =
		  hyphenatex (charactersWritten + cellsToWrite,
			      charactersWritten + availableCells);
	      if (breakAt)
		cellsToWrite = breakAt - charactersWritten;
	    }
	}
      for (k = charactersWritten; k < (charactersWritten + cellsToWrite); k++)
	if (translatedBuffer[k] == 0xa0)	/*unbreakable space */
	  translatedBuffer[k] = 0x20;	/*space */
      if (!insertWidechars
	  (&translatedBuffer[charactersWritten], cellsToWrite))
	return 0;
      charactersWritten += cellsToWrite;
      if (translatedBuffer[charactersWritten] == ' ')
	charactersWritten++;
      if ((breakAt && translatedBuffer[breakAt - 1] != *litHyphen)
	  || wordTooLong)
	{
	  if (!insertDubChars (litHyphen, strlen (litHyphen)))
	    return 0;
	}
      finishLine ();
    }
  return 1;
}

static int
doContents (void)
{
  int orig_translatedLength = 0;
  int charactersWritten = 0;
  int cellsToWrite = 0;
  int availableCells = 0;
  int k;
  int numbersLength = 0;
  int sameLine = 0;
  if ((!ud->braille_pages && !ud->print_pages) || ud->line_fill == ' ')
    {
      doLeftJustify ();
      return 1;
    }
  orig_translatedLength = translatedLength;
  for (k = translatedLength - 1; k > 0 && translatedBuffer[k] != 32; k--);
  if (k == 0)
    {
      doLeftJustify ();
      return 1;
    }
  translatedLength = k;
  while (charactersWritten < translatedLength)
    {
      int wordTooLong = 0;
      int breakAt = 0;
      int leadingBlanks = 0;
int lastLineCells;
      if (!sameLine)
	{
	  availableCells = startLine ();
	  if (styleSpec->status == startBody)
	    {
	      leadingBlanks = style->left_margin + style->first_line_indent;
	      styleSpec->status = resumeBody;
	    }
	  else
	    leadingBlanks = style->left_margin;
if (leadingBlanks < 0)
leadingBlanks = 0;
	  if (!insertCharacters (blanks, leadingBlanks))
	    return 0;
	  availableCells -= leadingBlanks;
lastLineCells = availableCells - (orig_translatedLength - 
translatedLength - 2);
	}
      if (numbersLength)
	{
	  if ((availableCells - numbersLength) <= 3)
	    insertCharacters (blanks, availableCells - numbersLength);
	  else
	    {
	      insertCharacters (blanks, 1);
	      for (k = availableCells - (numbersLength + 1); k > 0; k--)
		insertCharacters (&ud->line_fill, 1);
	      insertCharacters (blanks, 1);
	    }
	  availableCells = numbersLength;
	}
      if ((charactersWritten + availableCells) >= translatedLength)
	cellsToWrite = translatedLength - charactersWritten;
      else
	{
	  for (cellsToWrite = availableCells - 2; cellsToWrite > 0;
	       cellsToWrite--)
	    if (translatedBuffer[charactersWritten + cellsToWrite] == ' ')
	      break;
	  if (cellsToWrite == 0)
	    {
	      cellsToWrite = availableCells - 1;
	      wordTooLong = 1;
	    }
	  else
	    {
	      if (ud->hyphenate)
		breakAt =
		  hyphenatex (charactersWritten + cellsToWrite,
			      charactersWritten + availableCells);
	      if (breakAt)
		cellsToWrite = breakAt - charactersWritten;
	    }
	}
      for (k = charactersWritten; k < (charactersWritten + cellsToWrite); k++)
	if (translatedBuffer[k] == 0xa0)	/*unbreakable space */
	  translatedBuffer[k] = 0x20;	/*space */
      if (!insertWidechars
	  (&translatedBuffer[charactersWritten], cellsToWrite))
	return 0;
      charactersWritten += cellsToWrite;
      if (translatedBuffer[charactersWritten] == ' ')
	charactersWritten++;
      if ((breakAt && translatedBuffer[breakAt - 1] != *litHyphen)
	  || wordTooLong)
	{
	  if (!insertDubChars (litHyphen, strlen (litHyphen)))
	    return 0;
	}
      if (charactersWritten >= translatedLength && !numbersLength)
	{
	  numbersLength = orig_translatedLength - translatedLength;
	  translatedLength = orig_translatedLength;
	  if ((availableCells - cellsToWrite) < (numbersLength + 2))
	    finishLine ();
	  else
	    {
	      availableCells -= cellsToWrite;
	      sameLine = 1;
	    }
	  continue;
	}
      finishLine ();
    }
  return 1;
}

static int
doCenterRight (void)
{
  int charactersWritten = 0;
  int cellsToWrite = 0;
  int availableCells = 0;
  int k;
  while (charactersWritten < translatedLength)
    {
      int wordTooLong = 0;
      availableCells = startLine ();
      if ((translatedLength - charactersWritten) < availableCells)
	{
	  k = (availableCells - (translatedLength - charactersWritten));
	  if (style->format == centered)
	    k /= 2;
	  else if (style->format != rightJustified)
	    return 0;
	  if (!insertCharacters (blanks, k))
	    return 0;
	  if (!insertWidechars (&translatedBuffer[charactersWritten],
				translatedLength - charactersWritten))
	    return 0;
	  finishLine ();
	  break;
	}
      if ((charactersWritten + availableCells) > translatedLength)
	cellsToWrite = translatedLength - charactersWritten;
      else
	{
	  for (cellsToWrite = availableCells; cellsToWrite > 0;
	       cellsToWrite--)
	    if (translatedBuffer[charactersWritten + cellsToWrite] == ' ')
	      break;
	  if (cellsToWrite == 0)
	    {
	      cellsToWrite = availableCells - 1;
	      wordTooLong = 1;
	    }
	}
      for (k = charactersWritten; k < (charactersWritten + cellsToWrite); k++)
	if (translatedBuffer[k] == 0xa0)	/*unbreakable space */
	  translatedBuffer[k] = 0x20;	/*space */
      if (!wordTooLong)
	{
	  k = availableCells - cellsToWrite;
	  if (style->format == centered)
	    k /= 2;
	  if (!insertCharacters (blanks, k))
	    return 0;
	}
      if (!insertWidechars
	  (&translatedBuffer[charactersWritten], cellsToWrite))
	return 0;
      charactersWritten += cellsToWrite;
      if (translatedBuffer[charactersWritten] == ' ')
	charactersWritten++;
      if (wordTooLong)
	{
	  if (!insertDubChars (litHyphen, strlen (litHyphen)))
	    return 0;
	}
      finishLine ();
    }
  return 1;
}

static int
editTrans (void)
{
  if (!(ud->contents == 2) && !(style->format == computerCoded) &&
      *ud->edit_table_name && (ud->has_math || ud->has_chem || ud->has_music))
    {
      translationLength = ud->translated_length;
      translatedLength = MAX_TRANS_LENGTH;
      if (!lou_translateString (ud->edit_table_name,
				ud->translated_buffer,
				&translationLength, ud->text_buffer,
				&translatedLength, NULL, NULL, 0))
	{
	  ud->edit_table_name[0] = 0;
	  return 0;
	}
      translatedBuffer = ud->text_buffer;
    }
  else
    {
      translatedBuffer = ud->translated_buffer;
      translatedLength = ud->translated_length;
    }
  return 1;
}

static int
startStyle (void)
{
/*Line or page skipping before body*/
  styleSpec->status = startBody;
  if (!ud->paragraphs)
    return 1;
  if (ud->braille_pages && prevStyle->action != document)
    {
      if (style->righthand_page)
	{
	  fillPage ();
	  if (ud->interpoint && !(ud->braille_page_number & 1))
	    makeBlankPage ();
	}
      else if (style->newpage_before)
	fillPage ();
      else if (style->lines_before > 0
	       && prevStyle->lines_after == 0 && ud->lines_on_page > 0)
	{
	  if ((ud->lines_per_page - ud->lines_on_page) < 2)
	    fillPage ();
	  else if (!makeBlankLines (style->lines_before, 0))
	    return 0;
	}
    }
  else
    {
      if (style->lines_before > 0 && prevStyle->lines_after == 0 &&
	  prevStyle->action != document)
	{
	  if (!makeBlankLines (style->lines_before, 0))
	    return 0;
	}
    }
  writeOutbuf ();
  return 1;
}

static int
styleBody (void)
{
  sem_act action = style->action;
  while (ud->translated_length > 0 &&
	 ud->translated_buffer[ud->translated_length - 1] <= 32)
    ud->translated_length--;
  if (ud->translated_length == 0)
    return 1;
  if (!editTrans ())
    return 0;
  if (style->format != computerCoded && action != document)
    {
      int realStart;
      for (realStart = 0; realStart < translatedLength &&
	   translatedBuffer[realStart] <= 32 &&
	   translatedBuffer[realStart] != escapeChar; realStart++);
      if (realStart > 0)
	{
	  translatedBuffer = &translatedBuffer[realStart];
	  translatedLength -= realStart;
	}
    }
  while (translatedLength > 0
	 && translatedBuffer[translatedLength - 1] <= 32 &&
	 translatedBuffer[translatedLength - 1] != escapeChar)
    translatedLength--;
  if (translatedLength <= 0)
{
ud->translated_length = 0;
    return 1;
}
  if (!ud->paragraphs)
    {
      cellsWritten = 0;
      if (!insertWidechars (translatedBuffer, translatedLength))
	return 0;
      if (ud->interline)
	{
	  if (!doInterline ())
	    return 0;
	}
      else if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
	return 0;
      writeOutbuf ();
ud->translated_length = 0;
      return 1;
    }
  if (action == contentsheader && ud->contents != 2)
    {
      initialize_contents ();
      start_heading (action, translatedBuffer, translatedLength);
      finish_heading (action);
      ud->text_length = 0;
      ud->translated_length = 0;
      return 1;
    }
  if (ud->contents == 1)
    {
      if (ud->braille_pages && ud->braille_page_number_at && (action ==
							      heading1
							      || action ==
							      heading2
							      || action ==
							      heading3
							      || action ==
							      heading4))
	getBraillePageString ();
      start_heading (action, translatedBuffer, translatedLength);
    }
  switch (style->format)
    {
    case centered:
    case rightJustified:
      doCenterRight ();
      break;
    case alignColumnsLeft:
    case alignColumnsRight:
      doAlignColumns ();
      break;
    case listColumns:
      doListColumns ();
      break;
    case listLines:
      doListLines ();
      break;
    case computerCoded:
      doComputerCode ();
      break;
    case contents:
      doContents ();
      break;
    case leftJustified:
    default:
      doLeftJustify ();
      break;
    }
  writeOutbuf ();
  if (ud->contents == 1)
    finish_heading (action);
  styleSpec->status = resumeBody;
  ud->translated_length = 0;
  return 1;
}

static int
finishStyle (void)
{
/*Skip lines or pages after body*/
  if (ud->braille_pages)
    {
      if (style->newpage_after)
	fillPage ();
      else if (style->lines_after > 0)
	{
	  if ((ud->lines_per_page - ud->lines_on_page) < 2)
	    fillPage ();
	  else
	    {
	      if (!makeBlankLines (style->lines_after, 1))
		return 0;
	    }
	}
    }
  else
    {
      if (style->lines_after)
	{
	  if (!makeBlankLines (style->lines_after, 1))
	    return 0;
	}
    }
  writeOutbuf ();
  return 1;
}

int
write_paragraph (sem_act action)
{
  StyleType *holdStyle = action_to_style (action);
  if (!((ud->text_length > 0 || ud->translated_length > 0) &&
      ud->style_top >= 0))
    return 1;
  if (holdStyle == NULL)
    holdStyle = lookup_style ("para");
  /* We must do some of the work of start_styl**/
  if (ud->style_top < (STACKSIZE - 2))
    ud->style_top++;
  styleSpec = &ud->style_stack[ud->style_top];
  style = styleSpec->style = holdStyle;
  styleSpec->status = beforeBody;
  if (style->brlNumFormat != normal)
    ud->brl_page_num_format = style->brlNumFormat;
  styleSpec->curBrlNumFormat = ud->brl_page_num_format;
  startStyle ();
  insert_translation (ud->mainBrailleTable);
  styleBody ();
  end_style (holdStyle);
  return 1;
}

static char *xmlTags[] = {
  "<pagenum>", "</pagenum>", NULL
};

static int
insertEscapeChars (int number)
{
  int k;
  if (number <= 0)
    return 0;
  if ((ud->text_length + number) >= MAX_LENGTH)
    return 0;
  for (k = 0; k < number; k++)
    ud->text_buffer[ud->text_length++] = (widechar) escapeChar;
  return 1;
}

static int
makeParagraph (void)
{
  int translationLength = 0;
  int translatedLength;
  int charactersWritten = 0;
  int pieceStart;
  int k;
  while (ud->text_length > 0 && ud->text_buffer[ud->text_length - 1] <=
	 32 && ud->text_buffer[ud->text_length - 1] != escapeChar)
    ud->text_length--;
  if (ud->text_length == 0)
    return 1;
  ud->text_buffer[ud->text_length] = 0;
  k = 0;
  while (k < ud->text_length)
    {
      if (ud->text_buffer[k] == *litHyphen
	  && ud->text_buffer[k + 1] == 10
	  && ud->text_buffer[k + 2] != escapeChar)
	k += 2;
      if (k > translationLength)
	ud->text_buffer[translationLength] = ud->text_buffer[k];
      k++;
      translationLength++;
    }
  translatedLength = MAX_TRANS_LENGTH;
  if (!lou_backTranslateString (ud->mainBrailleTable,
				ud->text_buffer, &translationLength,
				&ud->translated_buffer[0],
				&translatedLength,
				(char *) ud->typeform, NULL, 0))
    return 0;
  if (ud->back_text == html)
    {
      if (!insertCharacters ("<p>", 3))
	return 0;
    }
  for (k = 0; k < translatedLength; k++)
    if (ud->translated_buffer[k] == 0)
      ud->translated_buffer[k] = 32;
  while (charactersWritten < translatedLength)
    {
      int lineLength;
      if ((charactersWritten + ud->back_line_length) > translatedLength)
	lineLength = translatedLength - charactersWritten;
      else
	{
	  lineLength = ud->back_line_length;
	  while (lineLength > 0
		 && ud->translated_buffer[charactersWritten +
					  lineLength] != 32)
	    lineLength--;
	  if (lineLength == 0)
	    {
	      lineLength = ud->back_line_length;
	      while ((charactersWritten + lineLength) < translatedLength
		     && ud->translated_buffer[charactersWritten +
					      lineLength] != 32)
		lineLength++;
	    }
	}
      pieceStart = charactersWritten;
      if (ud->back_text == html)
	{
	  for (k = charactersWritten; k < charactersWritten + lineLength; k++)
	    if (ud->translated_buffer[k] == '<'
		|| ud->translated_buffer[k] == '&'
		|| ud->translated_buffer[k] == escapeChar)
	      {
		if (!insertWidechars
		    (&ud->translated_buffer[pieceStart], k - pieceStart))
		  return 0;
		if (ud->translated_buffer[k] == '<')
		  {
		    if (!insertCharacters ("&lt;", 4))
		      return 0;
		  }
		else if (ud->translated_buffer[k] == '&')
		  {
		    if (!insertCharacters ("&amp;", 5))
		      return 0;
		  }
		else
		  {
		    int kk;
		    for (kk = k;
			 kk < translatedLength
			 && ud->translated_buffer[kk] == escapeChar; kk++);
		    kk -= k + 1;
		    if (!insertCharacters (xmlTags[kk], strlen (xmlTags[kk])))
		      return 0;
		    k += kk;
		  }
		pieceStart = k + 1;
	      }
	  if (!insertWidechars (&ud->translated_buffer[pieceStart], k -
				pieceStart))
	    return 0;
	}
      else
	{
	  if (!insertWidechars
	      (&ud->translated_buffer[charactersWritten], lineLength))
	    return 0;
	}
      charactersWritten += lineLength;
      if (ud->translated_buffer[charactersWritten] == 32)
	charactersWritten++;
      if (charactersWritten < translatedLength)
	{
	  if (ud->interline)
	    {
	      if (!doInterline ())
		return 0;
	    }
	  else if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
	    return 0;
	}
    }
  if (ud->back_text == html)
    {
      if (!insertCharacters ("</p>", 4))
	return 0;
    }
  if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
    return 0;
  if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
    return 0;
  writeOutbuf ();
  ud->text_length = 0;
  return 1;
}

static int
handlePrintPageNumber (void)
{
  int k, kk;
  int numberStart = 0;
  while (ud->text_length > 0 && ud->text_buffer[ud->text_length - 1] <= 32)
    ud->text_length--;
  for (k = ud->text_length - 1; k > 0; k--)
    {
      if (ud->text_buffer[k] == 10)
	break;
      if (ud->text_buffer[k] != '-')
	numberStart = k;
    }
  if ((numberStart - k) < 12)
    return 1;
  k++;
  if (ud->back_text == html)
    {
      widechar holdNumber[20];
      int kkk = 0;
      for (kk = numberStart; kk < ud->text_length; kk++)
	holdNumber[kkk++] = ud->text_buffer[kk];
      ud->text_length = k;
      if (!insertEscapeChars (1))
	return 0;
      for (kk = 0; kk < kkk; kk++)
	ud->text_buffer[ud->text_length++] = holdNumber[kk];
      if (!insertEscapeChars (2))
	return 0;
    }
  else
    {
      for (kk = numberStart; kk < ud->text_length; kk++)
	ud->text_buffer[k++] = ud->text_buffer[kk];
      ud->text_length = k;
    }
  return 1;
}

static int
discardPageNumber (void)
{
  int lastBlank = 0;
  int k;
  while (ud->text_length > 0 && ud->text_buffer[ud->text_length - 1] <= 32)
    ud->text_length--;
  for (k = ud->text_length - 1; k > 0 && ud->text_buffer[k] != 10; k--)
    {
      if (!lastBlank && ud->text_buffer[k] == 32)
	lastBlank = k;
      if (lastBlank && ud->text_buffer[k] > 32)
	break;
    }
  if (k > 0 && ud->text_buffer[k] != 10 && (lastBlank - k) > 2)
    ud->text_length = k + 2;
  return 1;
}

int
back_translate_file (void)
{
  int ch;
  int ppch = 0;
  int pch = 0;
  int leadingBlanks = 0;
  int printPage = 0;
  int newPage = 0;
  widechar outbufx[BUFSIZE];
  char *htmlStart = "<html><head><title>No Title</title></head><body>";
  char *htmlEnd = "</body></html>";
  if (!start_document ())
    return 0;
  ud->outbuf = outbufx;
  ud->outlen = MAX_LENGTH;
  if (ud->back_text == html)
    {
      if (!insertCharacters (htmlStart, strlen (htmlStart)))
	return 0;
      if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
	return 0;
      ud->output_encoding = utf8;
    }
  else
    ud->output_encoding = ascii8;
  while ((ch = fgetc (ud->inFile)) != EOF)
    {
      if (ch == 13)
	continue;
      if (pch == 10 && ch == 32)
	{
	  leadingBlanks++;
	  continue;
	}
      if (ch == escapeChar)
	ch = 32;
      if (ch == '[' || ch == '\\' || ch == '^' || ch == ']' || ch == '@'
	  || (ch >= 'A' && ch <= 'Z'))
	ch |= 32;
      if (ch == 10 && printPage)
	{
	  handlePrintPageNumber ();
	  printPage = 0;
	}
      if (ch == 10 && newPage)
	{
	  discardPageNumber ();
	  newPage = 0;
	}
      if (pch == 10 && (ch == 10 || leadingBlanks > 1))
	{
	  makeParagraph ();
	  leadingBlanks = 0;
	}
      if (!printPage && ppch == 10 && pch == '-' && ch == '-')
	printPage = 1;
      if (!newPage && pch == 10 && ch == ud->pageEnd[0])
	{
	  discardPageNumber ();
	  newPage = 1;
	  continue;
	}
      if (ch == 10)
	leadingBlanks = 0;
      ppch = pch;
      pch = ch;
      if (ud->text_length >= MAX_LENGTH)
	makeParagraph ();
      ud->text_buffer[ud->text_length++] = ch;
    }
  makeParagraph ();
  if (ud->back_text == html)
    {
      if (!insertCharacters (htmlEnd, strlen (htmlEnd)))
	return 0;
      if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
	return 0;
      writeOutbuf ();
      ud->output_encoding = ascii8;
    }
  return 1;
}

static int
makeLinkOrTarget (xmlNode * node, int which)
{
  StyleType *saveStyle;
  int saveFirst;
  int saveOutlen;
  xmlNode *child;
  int branchCount = 0;
  xmlChar *URL = get_attr_value (node);
  if (which == 0)
    insertCharacters ("<a href=\"", 9);
  else
    insertCharacters ("<a name=\"", 9);
  insertCharacters ((char *) URL, strlen ((char *) URL));
  insertCharacters ("\">", 2);
  saveOutlen = ud->outlen_so_far;
  child = node->children;
  while (child)
    {
      switch (child->type)
	{
	case XML_ELEMENT_NODE:
	  insert_code (node, branchCount);
	  branchCount++;
	  transcribe_paragraph (child, 1);
	  break;
	case XML_TEXT_NODE:
	  insert_text (child);
	  break;
	default:
	  break;
	}
      child = child->next;
    }
  insert_code (node, branchCount);
  insert_code (node, -1);
  insert_translation (ud->mainBrailleTable);
  saveStyle = style;
  saveFirst = styleSpec->status;
  styleSpec->status = startBody;
  style = lookup_style ("para");
  editTrans ();
  doLeftJustify ();
  style = saveStyle;
  styleSpec->status = saveFirst;
  if (ud->outlen_so_far > saveOutlen)
    ud->outlen_so_far -= strlen (ud->lineEnd);
  if (!insertCharacters ("</a>", 4))
    return 0;
  if (!insertCharacters (ud->lineEnd, strlen (ud->lineEnd)))
    return 0;
  writeOutbuf ();
  return 1;
}

int
insert_linkOrTarget (xmlNode * node, int which)
{
  fineFormat ();
  makeLinkOrTarget (node, which);
  return 1;
}

int
doBoxline (xmlNode * node)
{
  widechar boxChar;
  widechar boxLine[MAXNAMELEN];
  int k;
  int start = ud->text_length;
  int availableCells;
  insert_code (node, 0);
  if (!(ud->text_length - start))
    return 0;
  boxChar = ud->text_buffer[start];
  ud->text_length = start;
  cellsWritten = 0;
  availableCells = startLine ();
  while (availableCells != ud->cells_per_line)
    {
      finishLine ();
      availableCells = startLine ();
    }
  for (k = 0; k < availableCells; k++)
    boxLine[k] = boxChar;
  if (!insertWidechars (boxLine, availableCells))
    return 0;
  cellsWritten = ud->cells_per_line;
  finishLine ();
  return 1;
}

int
do_boxline (xmlNode * node)
{
  fineFormat ();
  return doBoxline (node);
}

int
do_newpage (void)
{
  fineFormat ();
  if (ud->lines_on_page > 0)
    fillPage ();
  return 1;
}

int
do_blankline (void)
{
  fineFormat ();
  makeBlankLines (1, 2);
  return 1;
}

int
do_softreturn (void)
{
  fineFormat ();
  return 1;
}

int
do_righthandpage (void)
{
  do_newpage ();
  if (ud->braille_pages && ud->interpoint && !(ud->braille_page_number & 1))
    fillPage ();
  return 1;
}

void
do_linespacing (xmlNode * node)
{
  widechar spacing;
  int savedTextLength = ud->text_length;
  insert_code (node, 0);
  if (ud->text_length == savedTextLength)
    spacing = '0';
  else
    spacing = ud->text_buffer[savedTextLength];
  ud->text_length = savedTextLength;
  if (spacing < '0' || spacing > '3')
    spacing = '0';
  ud->line_spacing = spacing - '0';
}

int
start_style (StyleType * curStyle)
{
  if (curStyle == NULL)
    curStyle = lookup_style ("para");
  if ((ud->text_length > 0 || ud->translated_length > 0) &&
      ud->style_top >= 0)
    {
      /*Continue last style */
      insert_translation (ud->mainBrailleTable);
      styleSpec = &ud->style_stack[ud->style_top];
      style = styleSpec->style;
      ud->brl_page_num_format = styleSpec->curBrlNumFormat;
      styleBody ();
    }
  if (ud->style_top < (STACKSIZE - 2))
    ud->style_top++;
  styleSpec = &ud->style_stack[ud->style_top];
  style = styleSpec->style = curStyle;
  styleSpec->status = beforeBody;
  if (style->brlNumFormat != normal)
    ud->brl_page_num_format = style->brlNumFormat;
  styleSpec->curBrlNumFormat = ud->brl_page_num_format;
  startStyle ();
  styleSpec->status = startBody;
  return 1;
}

int
end_style (StyleType * curStyle)
{
  if (curStyle == NULL)
    curStyle = lookup_style ("para");
  styleSpec = &ud->style_stack[ud->style_top];
  style = styleSpec->style;
  ud->brl_page_num_format = styleSpec->curBrlNumFormat;
  insert_translation (ud->mainBrailleTable);
  styleBody ();
  finishStyle ();
  memcpy (&prevStyleSpec, styleSpec, sizeof (prevStyleSpec));
  prevStyle = prevStyleSpec.style;
  ud->style_top--;
  if (ud->style_top < 0)
    ud->style_top = 0;
  styleSpec = &ud->style_stack[ud->style_top];
  style = styleSpec->style;
  ud->brl_page_num_format = styleSpec->curBrlNumFormat;
  return 1;
}
