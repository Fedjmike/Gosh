/*For fmemopen*/
#define _XOPEN_SOURCE 700
/*For asprintf*/
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <gc/gc.h>

#include "common.h"
#include "type.h"
#include "sym.h"
#include "ast.h"

#include "terminal.h"
#include "paths.h"
#include "dirctx.h"
#include "builtins.h"

#include "lexer.h"
#include "parser.h"
#include "analyzer.h"

#include "ast-printer.h"

#include "value.h"
#include "runner.h"
#include "display.h"

_Atomic unsigned int internalerrors = 0;

/*==== Compiler ====*/

typedef struct compilerCtx {
    typeSys ts;
    dirCtx dirs;

    sym* global;
} compilerCtx;

ast* compile (compilerCtx* ctx, const char* str, int* errors) {
    /*Store the error count ourselves if given a null ptr*/
    if (!errors)
        errors = &(int) {0};

    /*Turn the string into an AST*/
    ast* tree; {
        lexerCtx lexer = lexerInit(str);
        parserResult result = parse(ctx->global, &lexer);
        lexerDestroy(&lexer);

        tree = result.tree;
        *errors += result.errors;
    }

    /*Add types and other semantic information*/
    {
        analyzerResult result = analyze(&ctx->ts, tree);
        *errors += result.errors;
    }

    if (false)
    	printAST(tree);

    return tree;
}

compilerCtx compilerInit (void) {
    return (compilerCtx) {
        .ts = typesInit(),
        .dirs = dirsInit(initVectorFromPATH(), getWorkingDir()),
        .global = symInit()
    };
}

compilerCtx* compilerFree (compilerCtx* ctx) {
    symEnd(ctx->global);
    dirsFree(&ctx->dirs);
    typesFree(&ctx->ts);
    return ctx;
}

/*==== Gosh ====*/

typedef struct goshResult {
    value* v;
    type* dt;
} goshResult;

goshResult gosh (compilerCtx* ctx, const char* str, bool display) {
    errctx internalerrors = errcount();
    int errors = 0;

    ast* tree = compile(ctx, str, &errors);

    value* result = 0;
    type* dt = tree->dt;

    if (errors == 0 && no_errors_recently(internalerrors)) {
        /*Run the AST*/
        envCtx env = {};
        result = run(&env, tree);

        if (display)
            displayResult(result, dt);
    }

    astDestroy(tree);

    return (goshResult) {.v = result, .dt = dt};
}

/*==== REPL ====*/

typedef struct promptCtx {
    char* str;
    size_t size;
    const char* valid_for;
} promptCtx;

void writePrompt (promptCtx* prompt, const char* wdir, const char* homedir) {
    if (prompt->valid_for == wdir)
        return;

    /*Tilde contract the working directory*/
    char* wdir_contr = pathContract(wdir, homedir, "~", malloc);

    FILE* promptf = fmemopen(prompt->str, prompt->size, "w");
    fprintf_style(promptf, "{%s} $ ", styleYellow, wdir_contr);
    fclose(promptf);

    prompt->valid_for = wdir;
    free(wdir_contr);
}

void replCD (compilerCtx* compiler, const char* input) {
    int errors = 0;
    ast* tree = compile(compiler, input, &errors);

    if (errors || typeIsInvalid(tree->dt))
        ;

    else if (!typeIsKind(type_File, tree->dt))
        printf(":cd requires a File argument, given %s\n", typeGetStr(tree->dt));

    /*Types fine, try running it*/
    else {
        value* result = run(&(envCtx) {}, tree);

        if (!result || valueIsInvalid(result))
            ;

        else {
            const char* newWD = valueGetFilename(result);
            bool error = dirsChangeWD(&compiler->dirs, newWD);

            if (error)
                printf("Unable to enter directory \"%s\"\n", newWD);
        }
    }

    astDestroy(tree);
}

void replAST (compilerCtx* compiler, const char* input) {
    ast* tree = compile(compiler, input, 0);

    if (tree)
        printAST(tree);

    astDestroy(tree);
}

void replType (compilerCtx* compiler, const char* input) {
    int errors = 0;
    ast* tree = compile(compiler, input, &errors);

    if (tree && !errors)
        puts(typeGetStr(tree->dt));

    astDestroy(tree);
}

typedef struct replCommand {
    const char* name;
    size_t length;
    void (*handler)(compilerCtx* compiler, const char* input);
} replCommand;

static replCommand commands[] = {
    {"cd", strlen("cd"), replCD},
    {"ast", strlen("ast"), replAST},
    {"type", strlen("type"), replType}
};

void replCmd (compilerCtx* compiler, const char* input) {
    char* firstSpace = strchr(input, ' ');
    size_t cmdLength = firstSpace ? (size_t)(firstSpace - input) : strlen(input);

    if (cmdLength == 0) {
        printf("No command name given\n");
        return;
    }

    /*Look through all the commands...*/
    for (unsigned int i = 0; i < sizeof(commands) / sizeof(*commands); i++) {
        replCommand cmd = commands[i];

        /*... for one of the same length*/
        if (cmdLength != cmd.length)
            continue;

        /*... where the name matches*/
        bool nameMatches = !strncmp(input, cmd.name, cmd.length);

        if (nameMatches) {
            cmd.handler(compiler, input + cmd.length + 1);
            return;
        }
    }

    printf("No command named ':");
    /*Using printf precisions, %.*s, would convert the length to int*/
    fwrite(input, sizeof(*input), cmdLength, stdout);
    printf("'\n");
}

void repl (compilerCtx* compiler) {
    const char* homedir = getHomeDir();

    char* historyFilename;
    bool historyStaticStr = false;

    if (!precond(asprintf(&historyFilename, "%s/.gosh_history", homedir))) {
        historyFilename = "./.gosh_history";
        historyStaticStr = true;
    }

    read_history(historyFilename);

    promptCtx prompt = {.size = 1024};
    prompt.str = malloc(prompt.size);

    while (true) {
        /*Regenerate the prompt (if necessary)*/
        writePrompt(&prompt, compiler->dirs.workingDir, homedir);

        char* input = readline(prompt.str);

        /*Skip empty strings*/
        if (!input || input[0] == 0)
            continue;

        else if (!strcmp(input, ":exit"))
            break;

        add_history(input);
        write_history(historyFilename);

        if (input[0] == ':')
            replCmd(compiler, input+1);

        else
            gosh(compiler, input, true);
    }

    free(prompt.str);

    if (!historyStaticStr)
        free(historyFilename);
}

/*==== ====*/

int main (int argc, char** argv) {
    GC_INIT();

    rl_basic_word_break_characters = " \t\n\"\\'`@$><=;|&{([,";

    compilerCtx compiler = compilerInit();
    addBuiltins(&compiler.ts, compiler.global);

    if (argc == 1)
        repl(&compiler);

    else if (argc == 2)
        gosh(&compiler, argv[1], true);

    else {
        char* input = strjoinwith(argc, argv, " ", malloc);
        gosh(&compiler, input, true);
        free(input);
    }

    compilerFree(&compiler);
}
