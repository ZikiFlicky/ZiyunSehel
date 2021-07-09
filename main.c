#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAPE_SIZE 100

struct Tokenizer {
    /* what to read from */
    enum {
        ReadFromString = 1,
        ReadFromFile,
        ReadFromInput
    } read_type;
    /* a union storing the file/string+idx */
    union {
        FILE *file;
        char *string;
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
            ActionLoop,
            ActionTapeLeft,
            ActionTapeRight,
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

/*
  returns the current char of the stream
*/
static char tokenizer_get_char(struct Tokenizer *tokenizer) {
    char c;

    if (tokenizer->read_type == ReadFromString) {
        c = tokenizer->read_from.string[0];
        if (c == '\0')
            return '\0';
        ++tokenizer->read_from.string;
    } else if (tokenizer->read_type == ReadFromFile){
        if (feof(tokenizer->read_from.file))
            return '\0';
        c = fgetc(tokenizer->read_from.file);
    } else {
        /* TODO: add support for \r\n */
        c = fgetc(stdin);
        if (c == '\n')
            return '\0';
    }

    return c;
}

/*
  push a token into the tokenizer
*/
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
    perror("malloc/realloc");
    exit(1);
}

/*
  tokenize into tokenizer.tokens
*/
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

/*
  parse an action and return it
*/
static struct Action parser_parse_action(struct Parser *parser) {
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
                fprintf(stderr, "unmatched '['\n");
                exit(1);
            default:
                if (allocated == 0) {
                    allocated = 32;
                    tmp.loop_children = malloc(allocated * sizeof(struct Action));
                } else if (w_idx >= allocated) {
                    allocated += 8;
                    tmp.loop_children = realloc(tmp.loop_children, allocated * sizeof(struct Action));
                }
                if (parser->tokens[parser->token_read_idx] == TokenTypeCloseBracket) {
                    tmp.loop_children[w_idx++].action = ActionEnd;
                    goto end;
                }
                tmp.loop_children[w_idx++] = parser_parse_action(parser);
                break;
            }
        }
    end:
        break;
    }
    case TokenTypeCloseBracket:
        fprintf(stderr, "unmatched ']'\n");
        exit(1);
    }
    return tmp;
}

/*
  push an action into parser.actions
*/
static void parser_push(struct Parser *parser, struct Action action) {
    if (parser->allocated == 0) {
        parser->allocated = 64;
        parser->actions = malloc(parser->allocated * sizeof(struct Action));
    } else if (parser->idx >= parser->allocated) {
        parser->allocated += 16;
        parser->actions = realloc(parser->actions, parser->allocated * sizeof(struct Action));
    }
    parser->actions[parser->idx++] = action;
}

/*
  parse all actions and put them into parser.actions
*/
static void parser_parse(struct Parser *parser) {
    struct Action action;
    do {
        action = parser_parse_action(parser);
        parser_push(parser, action);
    } while (action.action != ActionEnd);
    /* free tokens, because they have no use anymore */
    free(parser->tokens);
}

/*
  high level function for parsing tokens
*/
static struct Action *parse_tokens(enum TokenType *tokens) {
    struct Parser parser;

    parser.tokens = tokens;
    parser.token_read_idx = 0;
    parser.idx = parser.allocated = 0;

    parser_parse(&parser);

    return parser.actions;
}

/*
  execute an action
*/
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
    /* it should never reach this, but it removes a warning */
    default:
        break;
    }
}

static void action_free(struct Action *action) {
    size_t i;
    if (action->action == ActionLoop) {
        for (i = 0; action->loop_children[i].action != ActionEnd; ++i)
            action_free(&action->loop_children[i]);
        free(action->loop_children);
        action->loop_children = NULL;
    }
}

static void interpreter_free(struct Interpreter *interpreter) {
    size_t i;
    for (i = 0; interpreter->actions[i].action != ActionEnd; ++i)
        action_free(&interpreter->actions[i]);
    free(interpreter->actions);
    interpreter->actions = NULL;
}

/*
  execute all of the actions one by one
*/
static void interpreter_execute(struct Interpreter *interpreter) {
    while (interpreter->actions[interpreter->action_read_idx].action != ActionEnd)
        interpreter_execute_action(interpreter, interpreter->actions[interpreter->action_read_idx++]);
    interpreter_free(interpreter);
}

static void interpreter_reset(struct Interpreter *interpreter) {
    size_t i;

    interpreter->action_read_idx = 0;
    interpreter->tape_idx = 0;
    for (i = 0; i < TAPE_SIZE; ++i)
        interpreter->tape[i] = 0;
}

static void execute_string(char *string) {
    struct Tokenizer tokenizer;
    struct Interpreter interpreter;

    /* set up tokenizer to read from a string, set the string's value and
       and reset all other attributes */
    tokenizer.read_type = ReadFromString;
    tokenizer.read_from.string = string;
    tokenizer.allocated = tokenizer.idx = 0;
    tokenizer_tokenize(&tokenizer);

    /* create interpreter and run it's actions */
    interpreter_reset(&interpreter);
    interpreter.actions = parse_tokens(tokenizer.tokens);
    interpreter_execute(&interpreter);
}

static void execute_file(const char *filename) {
    struct Tokenizer tokenizer;
    struct Interpreter interpreter;

    tokenizer.read_type = ReadFromFile;
    tokenizer.read_from.file = fopen(filename, "r"); /* open file */
    if (!tokenizer.read_from.file) {
        perror("fopen");
        exit(1);
    }
    tokenizer.allocated = tokenizer.idx = 0;
    tokenizer_tokenize(&tokenizer);
    fclose(tokenizer.read_from.file);

    /* create interpreter and run it's actions */
    interpreter_reset(&interpreter);
    interpreter.actions = parse_tokens(tokenizer.tokens);
    interpreter_execute(&interpreter);
}

static void execute_repl(void) {
    struct Tokenizer tokenizer;
    struct Interpreter interpreter;

    /* set up tokenizer to read from stdin and reset it's attributes */
    tokenizer.read_type = ReadFromInput;
    interpreter_reset(&interpreter);

    for (;;) {
        tokenizer.allocated = tokenizer.idx = 0;
        interpreter.action_read_idx = 0;
        printf("bf> ");
        tokenizer_tokenize(&tokenizer);
        interpreter.actions = parse_tokens(tokenizer.tokens);
        interpreter_execute(&interpreter);
    }
}

static void print_usage(void) {
    printf("run file: bf [filename]\n");
    printf("run string: bf -s [string of code to execute]\n");
    printf("run repl: bf -i\n");
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
    } else if (strcmp(argv[0], "-i") == 0) {
        ++argv;
        --argc;
        execute_repl();
    } else {
        for (i = 0; i < argc; ++i)
            execute_file(argv[i]);
    }
    return 0;
}

