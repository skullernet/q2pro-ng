/*
Copyright (C) 2003-2006 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

//
// prompt.c
//

#include "shared/shared.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/field.h"
#include "common/files.h"
#include "common/prompt.h"

#define MIN_MATCHES     64
#define MAX_MATCHES     250000000

typedef struct {
    const char *partial;
    int length, count;
    char **matches;
    completion_option_t options;
} genctx_t;

static genctx_t ctx;

static cvar_t   *com_completion_mode;
static cvar_t   *com_completion_treshold;

static void Prompt_ShowMatches(const commandPrompt_t *prompt, char **matches, int count)
{
    int numCols = 7, numLines;
    int i, j, k;
    size_t maxlen, len, total;
    size_t colwidths[6];

    // determine number of columns needed
    do {
        numCols--;
        numLines = (count + numCols - 1) / numCols;
        total = 0;
        for (i = 0; i < numCols; i++) {
            maxlen = 0;
            k = min((i + 1) * numLines, count);
            for (j = i * numLines; j < k; j++) {
                len = strlen(matches[j]);
                maxlen = max(maxlen, len);
            }
            maxlen += 2; // account for intercolumn spaces
            maxlen = min(maxlen, prompt->widthInChars);
            colwidths[i] = maxlen;
            total += maxlen;
        }
        if (total < prompt->widthInChars) {
            break; // this number of columns does fit
        }
    } while (numCols > 1);

    for (i = 0; i < numLines; i++) {
        for (j = 0; j < numCols; j++) {
            k = j * numLines + i;
            if (k >= count) {
                break;
            }
            prompt->printf("%*s", -(int)colwidths[j], matches[k]);
        }
        prompt->printf("\n");
    }
}

static void Prompt_ShowIndividualMatches(const commandPrompt_t *prompt, char **matches,
                                         int numCommands, int numAliases, int numCvars)
{
    if (numCommands) {
        qsort(matches, numCommands, sizeof(matches[0]), SortStrcmp);

        prompt->printf("\n%i possible command%s:\n",
                       numCommands, numCommands != 1 ? "s" : "");

        Prompt_ShowMatches(prompt, matches, numCommands);
        matches += numCommands;
    }

    if (numCvars) {
        qsort(matches, numCvars, sizeof(matches[0]), SortStrcmp);

        prompt->printf("\n%i possible variable%s:\n",
                       numCvars, numCvars != 1 ? "s" : "");

        Prompt_ShowMatches(prompt, matches, numCvars);
        matches += numCvars;
    }

    if (numAliases) {
        qsort(matches, numAliases, sizeof(matches[0]), SortStrcmp);

        prompt->printf("\n%i possible alias%s:\n",
                       numAliases, numAliases != 1 ? "es" : "");

        Prompt_ShowMatches(prompt, matches, numAliases);
        matches += numAliases;
    }
}

static bool ignore(const char *s)
{
    int i, r;

    if (ctx.count >= MAX_MATCHES)
        return true;

    if (!s || !*s)
        return true;

    if (ctx.options & CMPL_CASELESS)
        r = Q_strncasecmp(ctx.partial, s, ctx.length);
    else
        r = strncmp(ctx.partial, s, ctx.length);

    if (r)
        return true;

    if (!(ctx.options & CMPL_CHECKDUPS))
        return false;

    for (i = 0; i < ctx.count; i++) {
        if (ctx.options & CMPL_CASELESS)
            r = Q_strcasecmp(ctx.matches[i], s);
        else
            r = strcmp(ctx.matches[i], s);

        if (!r)
            return true;
    }

    return false;
}

static void add_match(char *s)
{
    if (!(ctx.count & (MIN_MATCHES - 1)))
        ctx.matches = Z_Realloc(ctx.matches, (ctx.count + MIN_MATCHES) * sizeof(char *));

    ctx.matches[ctx.count++] = s;
}

void Prompt_SetOptions(completion_option_t opt)
{
    ctx.options |= opt;
}

void Prompt_AddMatch(const char *s)
{
    if (ignore(s))
        return;

    add_match(Z_CopyString(s));
}

void Prompt_AddMatchNoAlloc(char *s)
{
    if (ignore(s)) {
        Z_Free(s);
        return;
    }

    add_match(s);
}

static bool needs_quotes(const char *s)
{
    int c;

    while (*s) {
        c = *s++;
        if (c == '$' || c == ';' || !Q_isgraph(c)) {
            return true;
        }
    }

    return false;
}

/*
====================
Prompt_CompleteCommand
====================
*/
void Prompt_CompleteCommand(commandPrompt_t *prompt, bool backslash)
{
    inputField_t *inputLine = &prompt->inputLine;
    char *first, *last, *text, **sorted;
    int i, j, c, pos, size, argnum;
    int numCommands, numCvars, numAliases;

    if (!inputLine->maxChars)
        return;

    text = inputLine->text;
    size = inputLine->maxChars + 1;
    pos = inputLine->cursorPos;

    // prepend backslash if missing
    if (backslash) {
        if (*text != '\\' && *text != '/') {
            memmove(text + 1, text, size - 1);
            *text = '\\';
        } else if (pos) {
            pos--;
        }
        text++;
        size--;
    }

    // skip previous parts if command line is multi-part
    for (i = j = c = 0; i < pos && text[i]; i++) {
        if (text[i] == '"')
            c ^= 1;
        else if (!c && text[i] == ';')
            j = i + 1;
    }
    if (j > 0) {
        text += j;
        size -= j;
        pos -= j;
    }

    // parse the input line into tokens
    Cmd_TokenizeString(text, false);

    // determine argument number to be completed
    argnum = Cmd_FindArgForOffset(pos);

    // generate matches
    memset(&ctx, 0, sizeof(ctx));
    ctx.partial = Cmd_Argv(argnum);
    ctx.length = strlen(ctx.partial);

    if (argnum) {
        // complete a command/cvar argument
        Com_Generic_c(0, argnum);
        numCommands = numCvars = numAliases = 0;
    } else {
        // complete a command/cvar/alias name
        Cmd_Command_g();
        numCommands = ctx.count;

        Cvar_Variable_g();
        numCvars = ctx.count - numCommands;

        Cmd_Alias_g();
        numAliases = ctx.count - numCvars - numCommands;
    }

    if (!ctx.count) {
        pos = strlen(inputLine->text);
        prompt->tooMany = false;
        goto finish; // nothing found
    }

    if (ctx.count > Cvar_ClampInteger(com_completion_treshold, 1, MAX_MATCHES) && !prompt->tooMany) {
        prompt->printf("Press TAB again to display all %d possibilities.\n", ctx.count);
        pos = strlen(inputLine->text);
        prompt->tooMany = true;
        goto finish;
    }

    prompt->tooMany = false;

    // truncate at current argument position
    text[Cmd_ArgOffset(argnum)] = 0;

    // append whitespace if completing a new argument
    if (argnum && argnum == Cmd_Argc()) {
        Q_strlcat(text, " ", size);
    }

    if (ctx.count == 1) {
        // we have finished completion!
        if (!(ctx.options & CMPL_STRIPQUOTES) && needs_quotes(ctx.matches[0])) {
            Q_strlcat(text, "\"", size);
            Q_strlcat(text, ctx.matches[0], size);
            Q_strlcat(text, "\"", size);
        } else {
            Q_strlcat(text, ctx.matches[0], size);
        }

        pos = strlen(inputLine->text);
        Q_strlcat(text, " ", size);

        // copy trailing arguments
        if (argnum + 1 < Cmd_Argc())
            Q_strlcat(text, Cmd_RawArgsFrom(argnum + 1), size);
        else
            pos++;
        goto finish;
    }

    // sort matches alphabethically
    sorted = Z_Malloc(ctx.count * sizeof(sorted[0]));
    memcpy(sorted, ctx.matches, ctx.count * sizeof(sorted[0]));
    qsort(sorted, ctx.count, sizeof(sorted[0]), (ctx.options & CMPL_CASELESS) ? SortStricmp : SortStrcmp);

    // add opening quote if needed
    for (i = 0; i < ctx.count; i++)
        if (needs_quotes(ctx.matches[i]))
            break;
    if (i < ctx.count)
        Q_strlcat(text, "\"", size);

    // copy matching part
    first = sorted[0];
    last = sorted[ctx.count - 1];
    do {
        if (*first != *last && (!(ctx.options & CMPL_CASELESS) || Q_tolower(*first) != Q_tolower(*last))) {
            break;
        }
        first++;
        last++;
    } while (*first);

    c = *first;
    *first = 0;
    Q_strlcat(text, sorted[0], size);
    *first = c;

    pos = strlen(inputLine->text);

    // copy trailing arguments
    if (argnum + 1 < Cmd_Argc()) {
        Q_strlcat(text, " ", size);
        Q_strlcat(text, Cmd_RawArgsFrom(argnum + 1), size);
    }

    prompt->printf("]\\%s\n", Cmd_ArgsFrom(0));
    if (argnum) {
        goto multi;
    }

    switch (com_completion_mode->integer) {
    case 0:
        // print in solid list
        for (i = 0; i < ctx.count; i++) {
            prompt->printf("%s\n", sorted[i]);
        }
        break;
    case 1:
    multi:
        // print in multiple columns
        Prompt_ShowMatches(prompt, sorted, ctx.count);
        break;
    case 2:
    default:
        // resort matches by type and print in multiple columns
        Prompt_ShowIndividualMatches(prompt, ctx.matches, numCommands, numAliases, numCvars);
        break;
    }

    Z_Free(sorted);

finish:
    // free matches
    for (i = 0; i < ctx.count; i++) {
        Z_Free(ctx.matches[i]);
    }
    Z_Free(ctx.matches);

    // move cursor
    inputLine->cursorPos = min(pos, inputLine->maxChars - 1);
}

void Prompt_CompleteHistory(commandPrompt_t *prompt, bool forward)
{
    const char *s, *m = NULL;
    unsigned i, j;

    if (!prompt->search) {
        s = prompt->inputLine.text;
        if (*s == '/' || *s == '\\') {
            s++;
        }
        if (!*s) {
            return;
        }
        prompt->search = Z_CopyString(s);
    }

    if (forward) {
        j = prompt->inputLineNum;
        if (prompt->historyLineNum == j) {
            return;
        }
        for (i = prompt->historyLineNum + 1; i != j; i++) {
            s = prompt->history[i & HISTORY_MASK];
            if (s && strstr(s, prompt->search)) {
                if (strcmp(s, prompt->inputLine.text)) {
                    m = s;
                    break;
                }
            }
        }
    } else {
        j = prompt->inputLineNum - HISTORY_SIZE;
        if (prompt->historyLineNum == j) {
            return;
        }
        for (i = prompt->historyLineNum - 1; i != j; i--) {
            s = prompt->history[i & HISTORY_MASK];
            if (s && strstr(s, prompt->search)) {
                if (strcmp(s, prompt->inputLine.text)) {
                    m = s;
                    break;
                }
            }
        }
    }

    if (!m) {
        return;
    }

    prompt->historyLineNum = i;
    IF_Replace(&prompt->inputLine, m);
}

void Prompt_ClearState(commandPrompt_t *prompt)
{
    prompt->tooMany = false;
    Z_Freep(&prompt->search);
}

/*
====================
Prompt_Action

User just pressed enter
====================
*/
char *Prompt_Action(commandPrompt_t *prompt)
{
    const char *s = prompt->inputLine.text;
    int i, j;

    Prompt_ClearState(prompt);
    if (s[0] == 0 || ((s[0] == '/' || s[0] == '\\') && s[1] == 0)) {
        IF_Clear(&prompt->inputLine);
        return NULL; // empty line
    }

    // save current line in history
    i = prompt->inputLineNum & HISTORY_MASK;
    j = (prompt->inputLineNum - 1) & HISTORY_MASK;
    if (!prompt->history[j] || strcmp(prompt->history[j], s)) {
        Z_Free(prompt->history[i]);
        prompt->history[i] = Z_CopyString(s);
        prompt->inputLineNum++;
    } else {
        i = j;
    }

    // stop history search
    prompt->historyLineNum = prompt->inputLineNum;

    IF_Clear(&prompt->inputLine);

    return prompt->history[i];
}

/*
====================
Prompt_HistoryUp
====================
*/
void Prompt_HistoryUp(commandPrompt_t *prompt)
{
    int i;

    Prompt_ClearState(prompt);

    if (prompt->historyLineNum == prompt->inputLineNum) {
        // save current line in history
        i = prompt->inputLineNum & HISTORY_MASK;
        Z_Free(prompt->history[i]);
        prompt->history[i] = Z_CopyString(prompt->inputLine.text);
    }

    if (prompt->inputLineNum - prompt->historyLineNum < HISTORY_SIZE &&
        prompt->history[(prompt->historyLineNum - 1) & HISTORY_MASK]) {
        prompt->historyLineNum--;
    }

    i = prompt->historyLineNum & HISTORY_MASK;
    IF_Replace(&prompt->inputLine, prompt->history[i]);
}

/*
====================
Prompt_HistoryDown
====================
*/
void Prompt_HistoryDown(commandPrompt_t *prompt)
{
    int i;

    Prompt_ClearState(prompt);

    if (prompt->historyLineNum == prompt->inputLineNum) {
        return;
    }

    prompt->historyLineNum++;

    i = prompt->historyLineNum & HISTORY_MASK;
    IF_Replace(&prompt->inputLine, prompt->history[i]);
}

/*
====================
Prompt_Clear
====================
*/
void Prompt_Clear(commandPrompt_t *prompt)
{
    int i;

    Prompt_ClearState(prompt);

    for (i = 0; i < HISTORY_SIZE; i++) {
        Z_Freep(&prompt->history[i]);
    }

    prompt->historyLineNum = 0;
    prompt->inputLineNum = 0;

    IF_Clear(&prompt->inputLine);
}

void Prompt_SaveHistory(const commandPrompt_t *prompt, const char *filename, int lines)
{
    qhandle_t f;
    const char *s;
    unsigned i;

    if (lines < 1) {
        return;
    }

    FS_OpenFile(filename, &f, FS_MODE_WRITE | FS_PATH_BASE);
    if (!f) {
        return;
    }

    if (lines > HISTORY_SIZE) {
        lines = HISTORY_SIZE;
    }

    for (i = prompt->inputLineNum - lines; i != prompt->inputLineNum; i++) {
        s = prompt->history[i & HISTORY_MASK];
        if (s && *s) {
            FS_FPrintf(f, "%s\n", s);
        }
    }

    FS_CloseFile(f);
}

void Prompt_LoadHistory(commandPrompt_t *prompt, const char *filename)
{
    char buffer[MAX_FIELD_TEXT];
    qhandle_t f;
    unsigned i;

    FS_OpenFile(filename, &f, FS_MODE_READ | FS_TYPE_REAL | FS_PATH_BASE | FS_DIR_HOME);
    if (!f) {
        return;
    }

    i = 0;
    while (1) {
        int len = FS_ReadLine(f, buffer, sizeof(buffer));
        if (len <= 0)
            break;
        while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
            len--;
        if (!len)
            continue;
        buffer[len] = 0;
        Z_Free(prompt->history[i & HISTORY_MASK]);
        prompt->history[i & HISTORY_MASK] = Z_CopyString(buffer);
        i++;
    }

    FS_CloseFile(f);

    prompt->historyLineNum = i;
    prompt->inputLineNum = i;
}

/*
====================
Prompt_Init
====================
*/
void Prompt_Init(void)
{
    com_completion_mode = Cvar_Get("com_completion_mode", "1", 0);
    com_completion_treshold = Cvar_Get("com_completion_treshold", "50", 0);
}
