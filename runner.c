#include "runner.h"

#include <sys/stat.h>

#include "ast.h"
#include "value.h"

static value* impl_size__ (value* file) {
    struct stat st;
    stat(file->filename, &st);

    return valueCreateInt(st.st_size);
}

value* runFnApp (envCtx* env, const ast* node) {
    value* result = run(env, node->l);

    for (int i = 0; i < node->children.length; i++) {
        ast* arg = vectorGet(node->children, i);
        result = valueCall(result, run(env, arg));
    }

    return result;
}

value* runLitStr (envCtx* env, const ast* node) {
    (void) env;

    //lol
    
    if (!strcmp(node->literal.str, "size")) {
        return valueCreateFn(impl_size__);

    } else {
        return valueCreateFile(node->literal.str);
    }
}

value* run (envCtx* env, const ast* node) {
    typedef value* (*handler_t)(envCtx*, const ast*);

    handler_t handler = (handler_t[]) {
        [astFnApp] = runFnApp,
        [astLitStr] = runLitStr
    }[node->kind];

    if (handler)
        return handler(env, node);

    else {
        errprintf("Unhandled AST kind, %d\n", node->kind);
        return 0;
    }
}
