#include "runner.h"

#include <sys/stat.h>

#include "sym.h"
#include "ast.h"
#include "value.h"

static value* impl_size__ (value* file) {
    const char* filename = valueGetFilename(file);

    if (!filename)
        return valueCreateInvalid();

    struct stat st;
    bool fail = stat(filename, &st);

    if (fail)
        return valueCreateInvalid();

    return valueCreateInt(st.st_size);
}

static value* runFnApp (envCtx* env, const ast* node) {
    value* result = run(env, node->l);

    for (int i = 0; i < node->children.length; i++) {
        ast* arg = vectorGet(node->children, i);
        result = valueCall(result, run(env, arg));
    }

    return result;
}

static value* runStrLit (envCtx* env, const ast* node) {
    (void) env;

    return valueCreateFile(node->literal.str);
}

static value* runListLit (envCtx* env, const ast* node) {
    vector(value*) result = vectorInit(node->children.length, GC_malloc);

    for (int i = 0; i < node->children.length; i++) {
        ast* element = vectorGet(node->children, i);
        vectorPush(&result, run(env, element));
    }

    return valueCreateVector(result);
}

static value* runSymbolLit (envCtx* env, const ast* node) {
    (void) env;

    if (!strcmp(node->literal.symbol->name, "size"))
        return valueCreateFn(impl_size__);

    else
        return valueCreateInvalid();
}

value* run (envCtx* env, const ast* node) {
    typedef value* (*handler_t)(envCtx*, const ast*);

    static handler_t table[astKindNo] = {
        [astFnApp] = runFnApp,
        [astStrLit] = runStrLit,
        [astListLit] = runListLit,
        [astSymbolLit] = runSymbolLit
    };

    handler_t handler = table[node->kind];

    if (handler)
        return handler(env, node);

    else {
        errprintf("Unhandled AST kind, %s\n", astKindGetStr(node->kind));
        return valueCreateInvalid();
    }
}
