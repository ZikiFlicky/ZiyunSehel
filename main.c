#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAPE_SIZE 100

struct Tokenizer {
    /* read from a string or from a file? */
    enum {
        ReadFromString = 1,
        ReadFromFile
    } read_type;
    /* a union storing the file/string+idx */
    union {
        FILE *file;
        struct {
            char *stream;
            size_t idx;
        } string;
    } read_from;

    enum TokenType {
        TokenTypeEof,
        TokenTypePlus,
        TokenTypeMinus,
        TokenTypeOpenBracket,
        TokenTypeCloseBracket,
        TokenTypeShiftLeft,
        TokenTypeShiftRight,
        TokenTypeDot,
        TokenTypeComma
    } *tokens;
    size_t allocated, idx;
};

struct Parser {
    enum TokenType *tokens;
    size_t token_read_idx;

    struct Action {
        enum ActionName {
            ActionEnd,
            ActionIncrement,
            ActionDecrement,
            ActionTapeLeft,
            ActionTapeRight,
            ActionLoop,
            ActionGetChar,
            ActionPutChar,
        } action;
        struct Action *loop_children;
    } *actions;
    size_t allocated, idx;
};

struct Interpreter {
    struct Action *actions;
    size_t action_read_idx;

    char tape[TAPE_SIZE];
    size_t tape_idx;
};

static char tokenizer_get_char(struct Tokenizer *tokenizer) {
    char c;

    if (tokenizer->read_type == ReadFromString) {
        c = tokenizer->read_from.string.stream[tokenizer->read_from.string.idx];
        if (c == '\0')
            return '\0';
        ++tokenizer->read_from.string.idx;
    } else {
        if (feof(tokenizer->read_from.file))
            return '\0';
        c = fgetc(tokenizer->read_from.file);
    }

    return c;
}

static void tokenizer_push_token(struct Tokenizer *tokenizer, enum TokenType token) {
    if (tokenizer->allocated == 0) {
        tokenizer->allocated = 64;
        tokenizer->tokens = malloc(tokenizer->allocated * sizeof(enum TokenType));
        if (!tokenizer->tokens)
            goto failure;
    } else if (tokenizer->idx >= tokenizer->allocated) {
        tokenizer->allocated += 16;
        tokenizer->tokens = realloc(tokenizer->tokens, tokenizer->allocated * sizeof(enum TokenType));
        if (!tokenizer->tokens)
            goto failure;
    }
    tokenizer->tokens[tokenizer->idx++] = token;
    return;
failure:
    perror("tokenizer_push_token");
    exit(1);
}

static void tokenizer_tokenize(struct Tokenizer *tokenizer) {
    char c;

    while ((c = tokenizer_get_char(tokenizer))) {
        switch (c) {
        case '+':
            tokenizer_push_token(tokenizer, TokenTypePlus);
            break;
        case '-':
            tokenizer_push_token(tokenizer, TokenTypeMinus);
            break;
        case '[':
            tokenizer_push_token(tokenizer, TokenTypeOpenBracket);
            break;
        case ']':
            tokenizer_push_token(tokenizer, TokenTypeCloseBracket);
            break;
        case '>':
            tokenizer_push_token(tokenizer, TokenTypeShiftRight);
            break;
        case '<':
            tokenizer_push_token(tokenizer, TokenTypeShiftLeft);
            break;
        case '.':
            tokenizer_push_token(tokenizer, TokenTypeDot);
            break;
        case ',':
            tokenizer_push_token(tokenizer, TokenTypeComma);
            break;
        }
    }
    tokenizer_push_token(tokenizer, TokenTypeEof);
}

static enum TokenType *tokenize_string(char *string) {
    struct Tokenizer tokenizer;

    tokenizer.read_type = ReadFromString;
    tokenizer.read_from.string.stream = string;
    tokenizer.read_from.string.idx = 0;
    tokenizer.allocated = tokenizer.idx = 0;
    tokenizer_tokenize(&tokenizer);

    return tokenizer.tokens;
}

static enum TokenType *tokenize_file(const char *filename) {
    struct Tokenizer tokenizer;

    tokenizer.read_type = ReadFromFile;
    tokenizer.read_from.file = fopen(filename, "r"); /* open file */
    if (!tokenizer.read_from.file) {
        perror("tokenize_file");
        exit(1);
    }
    tokenizer.allocated = tokenizer.idx = 0;
    tokenizer_tokenize(&tokenizer);
    fclose(tokenizer.read_from.file); /* close file */

    return tokenizer.tokens;
}

static struct Action parser_read_action(struct Parser *parser) {
    struct Action tmp;

    switch (parser->tokens[parser->token_read_idx++]) {
    case TokenTypeEof:
        tmp.action = ActionEnd;
        break;
    case TokenTypeShiftLeft:
        tmp.action = ActionTapeLeft;
        break;
    case TokenTypeShiftRight:
        tmp.action = ActionTapeRight;
        break;
    case TokenTypePlus:
        tmp.action = ActionIncrement;
        break;
    case TokenTypeMinus:
        tmp.action = ActionDecrement;
        break;
    case TokenTypeDot:
        tmp.action = ActionPutChar;
        break;
    case TokenTypeComma:
        tmp.action = ActionGetChar;
        break;
    case TokenTypeOpenBracket: {
        size_t allocated = 0, w_idx = 0;
        tmp.action = ActionLoop;
        for (;;) {
            switch (parser->tokens[parser->token_read_idx]) {
            case TokenTypeCloseBracket:
                ++parser->token_read_idx;
                goto end;
            case TokenTypeEof:
                fprintf(stderr, "Unmatched '['\n");
                exit(1);
            default:
                if (allocated == 0) {
                    allocated = 64;
                    tmp.loop_children = malloc(allocated * sizeof(struct Action));
                } else if (w_idx >= allocated) {
                    allocated += 8;
                    tmp.loop_children = realloc(tmp.loop_children, allocated * sizeof(struct Action));
                }
                tmp.loop_children[w_idx++] = parser_read_action(parser);
                break;
            }
        }
    end:
        if (w_idx >= allocated) {
            allocated = w_idx + 1;
            tmp.loop_children = realloc(tmp.loop_children, allocated * sizeof(struct Action));
            tmp.loop_children[w_idx].action = ActionEnd;
            ++w_idx;
        }
        break;
    }
    case TokenTypeCloseBracket:
        fprintf(stderr, "Unmatched ']'\n");
        exit(1);
    }
    return tmp;
}

static void parser_push(struct Parser *parser, struct Action action) {
    if (parser->allocated == 0) {
        parser->allocated = 64;
        parser->actions = malloc(parser->allocated * sizeof(struct Action));
    } else if (parser->idx >= parser->allocated) {
        parser->allocated += 8;
        parser->actions = realloc(parser->actions, parser->allocated * sizeof(struct Action));
    }
    parser->actions[parser->idx++] = action;
}

static void parser_parse(struct Parser *parser) {
    struct Action action;
    do {
        action = parser_read_action(parser);
        parser_push(parser, action);
    } while (action.action != ActionEnd);
    /* free tokens, because they aren't used anymore */
    free(parser->tokens);
}

static struct Action *parse_tokens(enum TokenType *tokens) {
    struct Parser parser;

    parser.tokens = tokens;
    parser.token_read_idx = 0;
    parser.idx = parser.allocated = 0;

    parser_parse(&parser);

    return parser.actions;
}

static void interpreter_execute_action(struct Interpreter *interpreter, struct Action action) {
    switch (action.action) {
    case ActionIncrement:
        ++interpreter->tape[interpreter->tape_idx];
        break;
    case ActionDecrement:
        --interpreter->tape[interpreter->tape_idx];
        break;
    case ActionTapeLeft:
        /* if the user is at the start of the tape, and wants to move left,
           they will get an error */
        if (interpreter->tape_idx == 0) {
            fprintf(stderr, "can't move left on the tape when at the first block\n");
            exit(1);
        }
        /* move the tape pointer left */
        --interpreter->tape_idx;
        break;
    case ActionTapeRight:
        /* if the user has reached the end of the tape, and wants to move right,
           they will get an error */
        if (interpreter->tape_idx == TAPE_SIZE - 1) {
            fprintf(stderr, "can't move right on the tape when at the last block\n");
            exit(1);
        }
        /* move the tape pointer right */
        ++interpreter->tape_idx;
        break;
    case ActionLoop: {
        size_t i;
        while (interpreter->tape[interpreter->tape_idx]) {
            for (i = 0; action.loop_children[i].action != ActionEnd; ++i)
                interpreter_execute_action(interpreter, action.loop_children[i]);
        }
        break;
    }
    case ActionGetChar:
        interpreter->tape[interpreter->tape_idx] = getchar();
        break;
    case ActionPutChar:
        putchar(interpreter->tape[interpreter->tape_idx]);
        break;
    }
}

static void interpreter_execute(struct Interpreter *interpreter) {
    while (interpreter->actions[interpreter->action_read_idx].action != ActionEnd) {
        interpreter_execute_action(interpreter, interpreter->actions[interpreter->action_read_idx++]);
    }
    free(interpreter->actions);
}

static void execute_actions(struct Action *actions) {
    struct Interpreter interpreter;
    size_t i;

    interpreter.actions = actions;
    interpreter.action_read_idx = 0;
    interpreter.tape_idx = 0;
    for (i = 0; i < TAPE_SIZE; ++i)
        interpreter.tape[i] = 0;
    interpreter_execute(&interpreter);
}

static void execute_string(char *string) {
    enum TokenType *tokens;
    struct Action *actions;

    tokens = tokenize_string(string);
    actions = parse_tokens(tokens);
    execute_actions(actions);
}

static void execute_file(const char *filename) {
    enum TokenType *tokens;
    struct Action *actions;

    tokens = tokenize_file(filename);
    actions = parse_tokens(tokens);
    execute_actions(actions);
}

static void print_usage(void) {
    printf("bf [filename]\n");
    printf("bf -s [string of code to execute]\n");
}

int main(int argc, char **argv) {
    size_t i;
    ++argv;
    --argc;
    if (argc == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(argv[0], "-s") == 0) {
        ++argv;
        --argc;
        if (argc == 0) {
            fprintf(stderr, "expected a string of code after '-s'\n");
            return 0;
        }
        for (i = 0; i < argc; ++i)
            execute_string(argv[i]);
    } else {
        for (i = 0; i < argc; ++i)
            execute_file(argv[i]);
    }
    return 0;
}

