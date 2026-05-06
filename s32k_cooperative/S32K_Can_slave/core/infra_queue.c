// 고정 크기 ring queue 구현 파일이다.
// queue는 호출자가 제공한 저장 공간에 item 전체를 복사하며,
// 기본은 단일 문맥 사용을 전제로 둔다.
#include "infra_queue.h"

#include <stddef.h>
#include <string.h>

// wrap-around를 고려해 ring index를 한 칸 전진시킨다.
// queue 포인터 계산을 한곳에 모아두면,
// push, pop, reset 로직을 작고 일관되게 유지할 수 있다.
static uint16_t InfraQueue_NextIndex(const InfraQueue *queue, uint16_t index)
{
    index++;
    if (index >= queue->capacity)
    {
        index = 0U;
    }

    return index;
}

// queue 메타데이터를 호출자가 제공한 저장 공간에 연결한다.
// 버퍼를 먼저 비워서,
// 모듈이 오래된 데이터 없이 예측 가능한 빈 queue 상태에서 시작하게 한다.
InfraStatus InfraQueue_Init(InfraQueue *queue,
                            void *storage,
                            uint16_t item_size,
                            uint16_t capacity)
{
    if ((queue == NULL) || (storage == NULL) || (item_size == 0U) || (capacity == 0U))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    queue->buffer = (uint8_t *)storage;
    queue->item_size = item_size;
    queue->capacity = capacity;
    queue->head = 0U;
    queue->tail = 0U;
    queue->count = 0U;

    (void)memset(queue->buffer, 0, (size_t)item_size * (size_t)capacity);

    return INFRA_STATUS_OK;
}

// queue를 다시 빈 상태로 되돌린다.
// 저장된 항목과 메타데이터를 함께 비워 두면,
// 재시작 뒤 오래된 payload가 뒤섞여 보이는 일을 줄일 수 있다.
void InfraQueue_Reset(InfraQueue *queue)
{
    if (queue == NULL)
    {
        return;
    }

    queue->head = 0U;
    queue->tail = 0U;
    queue->count = 0U;

    if ((queue->buffer != NULL) && (queue->item_size != 0U) && (queue->capacity != 0U))
    {
        (void)memset(queue->buffer,
                     0,
                     (size_t)queue->item_size * (size_t)queue->capacity);
    }
}

// ring queue 끝에 item 하나를 넣는다.
// queue가 가득 차면 호출자에게 바로 알려서,
// 재시도나 폐기 여부를 상위에서 결정하게 한다.
// ISR과 task가 함께 쓰면 이 구간은 짧은 크리티컬 섹션이 필요하다.
InfraStatus InfraQueue_Push(InfraQueue *queue, const void *item)
{
    uint8_t *slot;

    if ((queue == NULL) || (item == NULL) || (queue->buffer == NULL))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if (queue->count >= queue->capacity)
    {
        return INFRA_STATUS_FULL;
    }

    slot = &queue->buffer[(size_t)queue->tail * (size_t)queue->item_size];
    (void)memcpy(slot, item, queue->item_size);

    queue->tail = InfraQueue_NextIndex(queue, queue->tail);
    queue->count++;

    return INFRA_STATUS_OK;
}

// ring queue 앞에서 item 하나를 꺼낸다.
// 꺼낸 슬롯을 0으로 지워,
// 디버깅 때 오래된 데이터가 남아 보이지 않게 한다.
// 공유 접근이면 head/count 갱신 순서의 동시성 위험을 주의한다.
InfraStatus InfraQueue_Pop(InfraQueue *queue, void *out_item)
{
    uint8_t *slot;

    if ((queue == NULL) || (out_item == NULL) || (queue->buffer == NULL))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if (queue->count == 0U)
    {
        return INFRA_STATUS_EMPTY;
    }

    slot = &queue->buffer[(size_t)queue->head * (size_t)queue->item_size];
    (void)memcpy(out_item, slot, queue->item_size);
    (void)memset(slot, 0, queue->item_size);

    queue->head = InfraQueue_NextIndex(queue, queue->head);
    queue->count--;

    return INFRA_STATUS_OK;
}

// head 항목을 꺼내지 않고 그대로 들여다본다.
// consumer가 다음 항목을 보고 처리 경로를 고를 때
// queue 순서를 유지해야 하는 상황에서 사용한다.
InfraStatus InfraQueue_Peek(const InfraQueue *queue, void *out_item)
{
    const uint8_t *slot;

    if ((queue == NULL) || (out_item == NULL) || (queue->buffer == NULL))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if (queue->count == 0U)
    {
        return INFRA_STATUS_EMPTY;
    }

    slot = &queue->buffer[(size_t)queue->head * (size_t)queue->item_size];
    (void)memcpy(out_item, slot, queue->item_size);

    return INFRA_STATUS_OK;
}

// 현재 queue에 몇 개가 들어 있는지 반환한다.
// 호출자는 이 값으로 여유 공간을 보거나,
// 디버깅 중 backlog가 쌓이는지 간단히 확인할 수 있다.
uint16_t InfraQueue_GetCount(const InfraQueue *queue)
{
    if (queue == NULL)
    {
        return 0U;
    }

    return queue->count;
}

// queue가 담을 수 있는 총 슬롯 수를 알려 준다.
// count와 함께 보면 현재 사용량을 바로 계산할 수 있어,
// 상위 모듈이 capacity를 다시 들고 있을 필요가 줄어든다.
uint16_t InfraQueue_GetCapacity(const InfraQueue *queue)
{
    if (queue == NULL)
    {
        return 0U;
    }

    return queue->capacity;
}

// queue가 비어 있는지만 간단히 확인한다.
// 작은 poll loop에서는 상태 코드 비교 대신
// 이런 짧은 보조 함수는 흐름의 가독성을 높인다.
uint8_t InfraQueue_IsEmpty(const InfraQueue *queue)
{
    if (queue == NULL)
    {
        return 1U;
    }

    return (queue->count == 0U) ? 1U : 0U;
}

// queue가 가득 찼는지 확인한다.
// 호출자가 새 항목을 넣기 전에 여유를 간단히 확인할 때 쓴다.
uint8_t InfraQueue_IsFull(const InfraQueue *queue)
{
    if (queue == NULL)
    {
        return 1U;
    }

    return (queue->count >= queue->capacity) ? 1U : 0U;
}
