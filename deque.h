#ifndef __DEQUE_H__
#define __DEQUE_H__




typedef enum 
{
    DEQUE_OP_OK,
    DEQUE_OP_FAILED
}   deque_op_e;


typedef struct
{
    struct deque_node* next;
    struct deque_node_t* prev;
    char* payload;
    size_t capacity;
}   deque_node_t;


typedef struct
{
    deque_node_t* head;
    deque_node_t* tail;
    size_t        size;
}   deque_t;



deque_t*        initialize_deque();
deque_op_e      destroy_deque(deque_t* root);

deque_op_e      push_front(deque_t* root, char* payload, size_t payload_size);
deque_op_e      push_back(deque_t* root, char* payload, size_t payload_size);

int             pop_front(deque_t* root, char* dest, size_t capacity);
int             pop_back(deque_t* root, char* dest, size_t capacity);

int             peek_front(deque_t* root, char* dest, size_t capacity);
int             peek_back(deque_t* root, char* dest, size_t capacity);


size_t          deque_size(deque_t* root);



#endif
