#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
#include <errno.h>

#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0) -> Attribute)

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
const uint32_t TABLE_MAX_PAGES = 100;


typedef struct {
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;
typedef struct {
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1];
    char email[COLUMN_EMAIL_SIZE + 1];
} Row;
typedef enum {
    META_COMMAND_SUCCESS,
    META_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED_STATEMENT,
    PREPARE_SYNTAX_ERROR,
    PREPARE_STRING_TOO_LONG,
    PREPARE_NEGATIVE_ID
} PrepareResult;

typedef enum {
    STATEMENT_INSERT, STATEMENT_SELECT
} StatementType;
typedef enum { EXECUTE_SUCCESS, EXECUTE_TABLE_FULL, EXECUTE_DUPLICATE_KEY } ExecuteResult;

typedef enum {NODE_LEAF, NODE_INTERNAL} NodeType;

typedef struct {
    StatementType type;
    Row row_to_insert;
} Statement;

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const uint32_t PAGE_SIZE = 4096;


// Common Node Header Layout
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const uint32_t COMMON_NODE_HEADER_SIZE = NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

// Leaf Node Header Layout
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;

// Leaf Node Body Layout
const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET = LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS = LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;


typedef struct {
    int file_descriptor;
    uint32_t file_length;
    uint32_t num_pages;
    void *pages[TABLE_MAX_PAGES];
} Pager;

typedef struct {
    uint32_t num_rows;
    Pager* pager;
    uint32_t root_page_num;
} Table;


typedef struct {
    Table *table;
    uint32_t page_num;
    uint32_t cell_num;
    int end_of_table; // Indicates a position one past the last element
} Cursor;

void* get_page(Pager* pager, uint32_t page_num);
Cursor *table_start(Table* table);
void cursor_advance(Cursor* cursor);
InputBuffer * new_input_buffer();
void print_prompt();
void read_input(InputBuffer* input_buffer);
void close_input_buffer(InputBuffer* input_buffer);
void pager_flush(Pager *pager, uint32_t page_num);
void db_close(Table*table);
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table *table);
PrepareResult prepare_insert(InputBuffer* input_buffer, Statement *statement);
PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement);
void serialize_row(Row* source, void *destination);
void deserialize_row(void *source, Row* destination);
void* cursor_value(Cursor* cursor);
ExecuteResult execute_insert(Statement* statement, Table* table);
void print_row(Row* row);
ExecuteResult execute_select(Statement *statement, Table *table );
ExecuteResult execute_statement(Statement* statement, Table *table);
Pager * pager_open(const char* filename);
Table* db_open(const char* filename);
uint32_t* leaf_node_num_cells(void *node);
void* leaf_node_cell(void *node, uint32_t cell_num);
uint32_t* leaf_node_key(void *node, uint32_t cell_num);
void* leaf_node_value(void* node, uint32_t cell_num);
void initialize_leaf_node(void *node);
void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value);
void print_constants();
void print_leaf_node(void *node);
Cursor* table_find(Table* table, uint32_t key);
Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key);
NodeType get_node_type(void *node);
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value);

Cursor *table_start(Table* table){
    Cursor* cursor = malloc(sizeof(Cursor));
    cursor -> table = table;
    cursor -> page_num = table -> root_page_num;
    cursor -> cell_num = 0;

    void* root_node = get_page(table -> pager, table -> root_page_num);
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    cursor -> end_of_table = (num_cells == 0);
    return cursor;
}

// Return the position of the given key.
// If the key is not present, return the position
// where it should be inserted
Cursor *table_find(Table* table, uint32_t key){
    uint32_t root_page_num = table -> root_page_num;
    void *root_node = get_page(table -> pager, root_page_num);

    if (get_node_type(root_node) == NODE_LEAF){
        return leaf_node_find(table, root_page_num, key);
    } else {
        printf("Need to implementing searching an internal node\n");
        exit(EXIT_FAILURE);
    }
}

void cursor_advance(Cursor* cursor){
    uint32_t page_num = cursor -> page_num;
    void *node = get_page(cursor -> table -> pager, page_num);
    cursor->cell_num += 1;
    if (cursor->cell_num >= (*leaf_node_num_cells(node))){
        cursor -> end_of_table = 1;
    }
}

InputBuffer * new_input_buffer(){
    InputBuffer * inputBuffer = (InputBuffer*) malloc(sizeof(InputBuffer));
    inputBuffer -> buffer = NULL;
    inputBuffer -> buffer_length = 0;
    inputBuffer -> input_length = 0;

    return inputBuffer;
}


void print_prompt(){
    printf("db > ");
}

void read_input(InputBuffer* input_buffer){
    ssize_t bytes_read =
            getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
    if (bytes_read <= 0){
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    }

    input_buffer->input_length = bytes_read - 1;
    input_buffer->buffer[bytes_read-1] = 0;
}

void close_input_buffer(InputBuffer* input_buffer){
    free(input_buffer->buffer);
    free(input_buffer);
}

void pager_flush(Pager *pager, uint32_t page_num){
    if (pager -> pages[page_num] == NULL){
        printf("Tried to flush null page\n");
        exit(EXIT_FAILURE);
    }

    off_t offset = lseek(pager -> file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
    if (offset == -1){
        printf("Error seeking: %d\n", errno);
    }

    ssize_t bytes_written =
            write(pager -> file_descriptor, pager -> pages[page_num], PAGE_SIZE);

    if (bytes_written == -1){
        printf("Error writing %d\n", errno);
        exit(EXIT_FAILURE);
    }

}

void db_close(Table*table){
    Pager *pager = table->pager;
    for (uint32_t i = 0; i < pager -> num_pages; i++){
        if (pager -> pages[i] == NULL){
            continue;
        }
        pager_flush(pager, i);
        free(pager -> pages[i]);
        pager -> pages[i] = NULL;
    }

//    uint32_t num_additional_rows = table -> num_rows % ROWS_PER_PAGE;
//    if (num_additional_rows > 0){
//        uint32_t page_num = num_full_pages;
//        if (pager -> pages[page_num] != NULL){
//            pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
//            free(pager -> pages[page_num]);
//            pager -> pages[page_num] = NULL;
//        }
//    }
    int result = close(pager -> file_length);
    if (result == -1){
        printf("Error closing db file.\n");
        exit(EXIT_FAILURE);
    }

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++){
        void* page = pager -> pages[i];
        if (page){
            free(page);
            pager -> pages[i] = NULL;
        }
    }

    free(pager);
    free(table);
}

MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table *table){
    if (strcmp(input_buffer -> buffer, ".exit") == 0){
        close_input_buffer(input_buffer);
        db_close(table);
        exit(EXIT_SUCCESS);
    } else if(strcmp(input_buffer -> buffer, ".constants") == 0){
        printf("Constants: \n");
        print_constants();
        return META_COMMAND_SUCCESS;
    } else if(strcmp(input_buffer -> buffer, ".btree") == 0){
        printf("Tree:\n");
        print_leaf_node(get_page(table -> pager, 0));
        return META_COMMAND_SUCCESS;
    } else {
        return META_UNRECOGNIZED_COMMAND;
    }
}

PrepareResult prepare_insert(InputBuffer* input_buffer, Statement *statement){
    statement->type = STATEMENT_INSERT;

    char *keyword = strtok(input_buffer -> buffer, " ");
    char *id_string = strtok(NULL, " ");
    char *username = strtok(NULL, " ");
    char *email = strtok(NULL, " ");

    if (id_string == NULL || username == NULL || email == NULL){
        return PREPARE_SYNTAX_ERROR;
    }

    int id = atoi(id_string);

    if (id < 0){
        return PREPARE_NEGATIVE_ID;
    }

    if (strlen(username) > COLUMN_USERNAME_SIZE){
        return PREPARE_STRING_TOO_LONG;
    }

    if (strlen(email) > COLUMN_EMAIL_SIZE){
        return PREPARE_STRING_TOO_LONG;
    }

    statement -> row_to_insert.id = id;
    strcpy(statement -> row_to_insert.username, username);
    strcpy(statement -> row_to_insert.email, email);

    return PREPARE_SUCCESS;

}

PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement){
    if (strncmp(input_buffer -> buffer, "insert", 6) == 0){
        return prepare_insert(input_buffer, statement);
    }
    if (strcmp(input_buffer-> buffer, "select") == 0){
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}



void print_constants(){
    printf("ROW_SIZE: %d\n", ROW_SIZE);
    printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
    printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
    printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
    printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
    printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
}

void serialize_row(Row* source, void *destination){
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    strncpy(destination + USERNAME_OFFSET, source -> username, USERNAME_SIZE);
    strncpy(destination + EMAIL_OFFSET, source -> email, EMAIL_SIZE);
}

void deserialize_row(void *source, Row* destination){
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}


void* get_page(Pager* pager, uint32_t page_num){
    if (page_num > TABLE_MAX_PAGES){
        printf("Tried to fetch page number out of bounds. %d > %d\n", page_num, TABLE_MAX_PAGES);
        exit(EXIT_FAILURE);
    }

    if (pager -> pages[page_num] == NULL){
        // Cache miss. Allocate memory and load from file.
        // We might save a partial page at the end of the file

        void *page = malloc(PAGE_SIZE);
        uint32_t num_pages = pager-> file_length / PAGE_SIZE;
        if (pager -> file_length % PAGE_SIZE){
            num_pages+=1;
        }

        if (page_num <= num_pages){
            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
            if (bytes_read == -1){
                printf("Error reading file: %d\n", errno);
                exit(EXIT_FAILURE);
            }
        }
        pager -> pages[page_num] = page;

        if (page_num >= pager -> num_pages){
            pager -> num_pages = page_num + 1;
        }
    }
    return pager -> pages[page_num];
}

void* cursor_value(Cursor* cursor){
    uint32_t page_num = cursor -> page_num;
    void *page = get_page(cursor -> table -> pager, page_num);
    return leaf_node_value(page, cursor -> cell_num);
}
ExecuteResult execute_insert(Statement* statement, Table* table){
    void *node = get_page(table -> pager, table -> root_page_num);
    uint32_t num_cells = (*leaf_node_num_cells(node));

    Row *row_to_insert = &(statement->row_to_insert);
    uint32_t key_to_insert = row_to_insert -> id;
    Cursor *cursor = table_find(table, key_to_insert);

    if (cursor -> cell_num < num_cells){
        uint32_t key_at_index = *leaf_node_key(node, cursor -> cell_num);
        if (key_at_index == key_to_insert){
            return EXECUTE_DUPLICATE_KEY;
        }
    }

    leaf_node_insert(cursor, row_to_insert -> id, row_to_insert);
    free(cursor);

    return EXECUTE_SUCCESS;

}

NodeType get_node_type(void* node){
    uint8_t value = *((uint8_t*) (node + NODE_TYPE_OFFSET));
    return value;
}

void set_node_type(void *node, NodeType type){
    uint8_t value = type;
    *((uint8_t*)(node + NODE_TYPE_OFFSET)) = value;
}

void print_row(Row* row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}
ExecuteResult execute_select(Statement *statement, Table *table ){
    Cursor *cursor = table_start(table);
    Row row;
    while(!(cursor -> end_of_table)){
        deserialize_row(cursor_value(table), &row);
        print_row(&row);
        cursor_advance(cursor);
    }

    free(cursor);
    return EXECUTE_SUCCESS;

}

ExecuteResult execute_statement(Statement* statement, Table *table){
    switch (statement -> type) {
        case STATEMENT_INSERT:
            return execute_insert(statement, table);
        case STATEMENT_SELECT:
            return execute_select(statement, table);
    }
}

Pager * pager_open(const char* filename){
    int fd = open(filename, O_RDWR | O_CREAT | S_IWUSR | S_IRUSR);
    if (fd == -1){
        printf("Unable to open file\n");
        exit(EXIT_FAILURE);
    }

    off_t file_length = lseek(fd, 0, SEEK_END);
    Pager* pager = malloc(sizeof(Pager));
    pager -> file_descriptor = fd;
    pager -> file_length = file_length;
    pager -> num_pages = (file_length / PAGE_SIZE);

    if (file_length % PAGE_SIZE % PAGE_SIZE != 0){
        printf("Db file is not a whole number of pages. Corrupt file.\n");
        exit(EXIT_FAILURE);
    }

    for(uint32_t i = 0; i < TABLE_MAX_PAGES; i++){
        pager->pages[i] = NULL;
    }

    return pager;
}

Table* db_open(const char* filename) {

    Pager* pager = pager_open(filename);
    Table *table = malloc(sizeof(Table));
    table -> pager = pager;
    table -> root_page_num = 0;

    if (pager -> num_pages == 0){
        void *root_node = get_page(pager, 0);
        initialize_leaf_node(root_node);
    }
    return table;
}


// B_TREE_IMPLEMENTATION Start

uint32_t* leaf_node_num_cells(void *node){
    return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

void* leaf_node_cell(void *node, uint32_t cell_num){
    return node + LEAF_NODE_HEADER_SIZE + cell_num + LEAF_NODE_CELL_SIZE;
}

uint32_t* leaf_node_key(void *node, uint32_t cell_num){
    return leaf_node_cell(node, cell_num);
}

void* leaf_node_value(void* node, uint32_t cell_num){
    return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

void initialize_leaf_node(void *node){
    set_node_type(node, NODE_LEAF);
    *leaf_node_num_cells(node) = 0;
}

void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value){
    void *node = get_page(cursor -> table -> pager, cursor -> page_num);

    uint32_t num_cells = *leaf_node_num_cells(node);
    if (num_cells >= LEAF_NODE_MAX_CELLS){
        // Node full
        leaf_node_split_and_insert(cursor, key, value);
        return;
    }

    if (cursor -> cell_num < num_cells){
        // Make a room for new cell
        for (uint32_t i = num_cells; i > cursor -> cell_num; i--){
            memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1), LEAF_NODE_CELL_SIZE);
        }

        *(leaf_node_num_cells(node)) += 1;
        *(leaf_node_key(node, cursor -> cell_num)) = key;
        serialize_row(value, leaf_node_value(node, cursor -> cell_num));
    }
}

void print_leaf_node(void* node){
    uint32_t num_cells = *leaf_node_num_cells(node);
    printf("leaf (size %d)\n", num_cells);
    for(uint32_t i = 0; i < num_cells; i++){
        uint32_t key = *leaf_node_key(node, i);
        printf("  - %d : %d\n", i, key);
    }
}

Cursor* leaf_node_find(Table*table, uint32_t page_num, uint32_t key){
    void *node = get_page(table -> pager, page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);

    Cursor* cursor = malloc(sizeof(Cursor));
    cursor -> table = table;
    cursor -> page_num = page_num;

    // Binary Search

    uint32_t min_index = 0;
    uint32_t one_past_max_index = num_cells;
    while(one_past_max_index != min_index){
        uint32_t index = (min_index + one_past_max_index) / 2;
        uint32_t key_at_index = *leaf_node_key(node, index);
        if (key == key_at_index){
            cursor -> cell_num = index;
            return cursor;
        }

        if (key < key_at_index){
            one_past_max_index = index;
        } else {
            min_index = index + 1;
        }
    }

    cursor -> cell_num = min_index;
    return cursor;
}

void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value){
    // Create a new node and move half the cells over
    // Insert the new value in one of the two nodes.
    // Update parent or create a new parent.

    void *old_node = get_page(cursor -> table -> pager, cursor -> page_num);
    uint32_t new_page_num = get_unused_page_num(cursor -> table -> pager);
    void *new_node = get_page(cursor -> table -> pager, new_page_num);
    initialize_leaf_node(new_node);

    // All existing keys plus new key should be divided evenly between old (left) and new (right) nodes.
    // Starting from the right, move each key to correct position.

    for (uint32_t i = LEAF_NODE_MAX_CELLS; i >= 0; i--){
        void *destination_node;

        if (i >= LEAF_NODE_LEFT_SPLIT_COUNT){
            destination_node = new_node;
        } else {
            destination_node = old_node;
        }

        uint32_t index_within_node = i % LEAF_NODE_LEFT_SPLIT_COUNT;
        void *destination = leaf_node_cell(destination_node, index_within_node);

        if (i == cursor->cell_num){
            serialize_row(value, destination);
        } else if (i > cursor -> cell_num){
            memcpy(destination, leaf_node_cell(old_node, i - 1), LEAF_NODE_CELL_SIZE);
        } else {
            memcpy(destination, leaf_node_cell(old_node, i), LEAF_NODE_CELL_SIZE);
        }
    }

    *(leaf_node_num_cells(old_node)) = LEAF_NODE_LEFT_SPLIT_COUNT;
    *(leaf_node_num_cells(new_node)) = LEAF_NODE_RIGHT_SPLIT_COUNT;
}
// =================================== End

int main(int argc, char* argv[]) {
    if (argc < 2){
        printf("Must supply a database filename.\n");
        exit(EXIT_FAILURE);
    }
    char *filename = argv[1];
    Table *table = db_open(filename);

    InputBuffer * input_buffer = new_input_buffer();
    while (1) {
        print_prompt();
        read_input(input_buffer);

        if (input_buffer->buffer[0] == '.'){
            switch (do_meta_command(input_buffer, table)) {
                case (META_COMMAND_SUCCESS):
                    continue;
                case (META_UNRECOGNIZED_COMMAND):
                    printf("Unrecognized command '%s'\n", input_buffer->buffer);
                    continue;
            }
        }
        Statement  statement;
        switch (prepare_statement(input_buffer, &statement)) {
            case PREPARE_SUCCESS:
                break;
            case PREPARE_STRING_TOO_LONG:
                printf("String is too long.\n");
                continue;
            case PREPARE_NEGATIVE_ID:
                printf("ID must be positive.\n");
                continue;
            case PREPARE_SYNTAX_ERROR:
                printf("Syntax error. Could not parse statement .\n");
                continue;
            case PREPARE_UNRECOGNIZED_STATEMENT:
                printf("Unrecognized keyword at start of '%s' .\n", input_buffer->buffer);
                continue;
        }

        switch (execute_statement(&statement, table)) {
            case EXECUTE_SUCCESS:
                printf("Executed .\n");
                break;
            case EXECUTE_DUPLICATE_KEY:
                printf("Error: Duplicate key.\n");
                break;
            case EXECUTE_TABLE_FULL:
                printf("Error: Table full.\n");
                break;
        }
    }
}