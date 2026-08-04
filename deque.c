#include "allocator.h"
#include "deque.h"



deque_node_t* initialize_deque()
{
    deque_t* root = (deque_t*)kMalloc(sizeof(deque_t));
    root->head = NULL;
    root->tail = NULL;
    root->size = 0;
    

    return root;
}



deque_op_e destroy_deque(deque_t* root)
{
    while(root->head != root->tail)
    {
        deque_node_t* tmp = root->head;
        kFree(tmp->payload);
        root->head = root->head->prev;
        kFree(tmp);
    }

    if(root->head != NULL)
    {
        kFree(root->head->payload);
        kFree(root->head);
    }

    kFree(root);

    return DEQUE_OP_OK;
}



deque_op_e push_front(deque_t* root, char* payload, size_t payload_size)
{
    deque_node_t* new_node = (deque_node_t*)kMalloc(sizeof(deque_node_t));
    new_node->payload = payload;
    new_node->capacity = payload_size;

    if(root->size == 0)
    {
        root->tail = new_node;
        root->head = new_node;
        new_node->next = NULL;
        new_node->prev = NULL;
    }

    else
    {
        root->head->next = new_node;
        new_node->next = NULL;
        new_node->prev = root->head;
        root->head = new_node;
    }

    root->size++;

    return DEQUE_OP_OK
}



deque_op_e push_back(deque_t* root, char* payload, size_t payload_size)
{
    deque_node_t* new_node = (deque_node_t*)kMalloc(sizeof(deque_node_t));
    new_node->payload = payload;
    new_node->capacity = payload_size;

    if(root->size == 0)
    {
        root->tail = new_node;
        root->head = new_node;
        new_node->next = NULL;
        new_node->prev = NULL;
    }

    else
    {
        root->tail->prev = new_node;
        new_node->next = root->tail;
        new_node->prev = NULL;
        root->tail = new_node;
    }

    root->size++;

    return DEQUE_OP_OK
}



deque_op_e pop_front(deque_t* root, char** dest, size_t* capacity)
{
    if ((root->size < 1) || (capacity < root->head->capacity)) return -1; 

    *capacity = root->head->capacity;
    *dest = root->head->payload;
    
    deque_node_t* tmp = root->head;
    root->head = root->head->prev;

    if (root->head == NULL) root->tail = NULL;
    else root->head->next = NULL;

    kFree(tmp);

    root->size--;

    return DEQUE_OP_OK;
}



deque_op_e pop_back(deque_t* root, char** dest, size_t* capacity)
{
    if ((root->size < 1) || (capacity < root->tail->capacity)) return -1; 

    *capacity = root->tail->capacity;
    *dest = root->tail->payload;
    
    deque_node_t* tmp = root->tail;
    root->tail = root->tail->next;

    if (root->tail == NULL) root->head = NULL;
    else root->tail->prev = NULL;

    kFree(tmp);

    root->size--;

    return DEQUE_OP_OK;
}



deque_op_e peek_front(deque_t* root, char** dest, size_t* capacity)
{
    if ((root->size < 1) || (capacity < root->head->capacity)) return -1; 

    *capacity = root->head->capacity;
    *dest = root->head->payload;

    return DEQUE_OP_OK;
}



deque_op_e peek_back(deque_t* root, char** dest, size_t* capacity)
{
    if ((root->size < 1) || (capacity < root->tail->capacity)) return -1; 

    *capacity = root->tail->capacity;
    *dest = root->tail->payload;

    return DEQUE_OP_OK;
}




size_t deque_size(deque_t* root)
{
    return root->size; 
}
