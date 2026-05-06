// 고정 크기 ring queue 추상화를 제공하는 헤더다.
// 여러 모듈이 이 저장 구조를 재사용하여
// 동적 메모리 없이 메시지를 버퍼링할 수 있게 한다.
#ifndef INFRA_QUEUE_H
#define INFRA_QUEUE_H

#include "infra_types.h"

// 범용 ring queue 메타데이터 구조체다.
// 실제 저장 공간은 구조체 밖에 두어서,
// 모듈이 queue 소유권을 명확히 유지한 채 공통 구현을 재사용할 수 있다.
typedef struct
{
    uint8_t  *buffer;
    uint16_t  item_size;
    uint16_t  capacity;
    uint16_t  head;
    uint16_t  tail;
    uint16_t  count;
} InfraQueue;

// 호출자가 소유한 ring queue를 초기화한다.
// 모듈은 보통 시작 직후 시 한 번 호출하여,
// queue 메타데이터를 자신이 가진 고정 저장 공간에 연결한다.
InfraStatus InfraQueue_Init(InfraQueue *queue,
                            void *storage,
                            uint16_t item_size,
                            uint16_t capacity);
// InfraQueue 상태를 초기값으로 되돌린다.
void        InfraQueue_Reset(InfraQueue *queue);
// InfraQueue에 항목 하나를 적재한다.
InfraStatus InfraQueue_Push(InfraQueue *queue, const void *item);
// InfraQueue이 보관한 항목 하나를 꺼낸다.
InfraStatus InfraQueue_Pop(InfraQueue *queue, void *out_item);
// InfraQueue 현재 항목을 제거하지 않고 조회한다.
InfraStatus InfraQueue_Peek(const InfraQueue *queue, void *out_item);
// InfraQueue에 쌓인 개수를 조회한다.
uint16_t    InfraQueue_GetCount(const InfraQueue *queue);
// InfraQueue 용량을 조회한다.
uint16_t    InfraQueue_GetCapacity(const InfraQueue *queue);
// InfraQueue이 비어 있는지 확인한다.
uint8_t     InfraQueue_IsEmpty(const InfraQueue *queue);
// InfraQueue이 가득 찼는지 확인한다.
uint8_t     InfraQueue_IsFull(const InfraQueue *queue);

#endif
