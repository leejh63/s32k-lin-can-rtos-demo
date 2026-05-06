// app 계층을 위한 CAN request 버퍼링 구현 파일이다.
// scheduler tick마다 CanService로 넘어가는 작업량을 조절하여,
// 애플리케이션 코드가 transport 세부보다 의도 표현에 집중할 수 있게 한다.
#include "can_module_internal.h"

#include <stddef.h>
#include <string.h>

// app 수준 CAN 요청 하나를 소프트웨어 buffer에 적재한다.
// 이렇게 하면 app 코드가 service 가용성과 분리되고,
// module이 CAN service로의 작업 유입을 점진적으로 조절할 수 있다.
static InfraStatus CanModule_PushRequest(CanModule *module, const CanModuleRequest *request)
{
    if ((module == NULL) || (request == NULL) || (module->initialized == 0U))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    return InfraQueue_Push(&module->request_queue, request);
}

// queue에 쌓인 app 요청을 하위 CAN service로 옮긴다.
// tick당 제출량을 제한해 두어,
// 한 번의 app task가 transport 작업을 한꺼번에 독점하지 못하게 한다.
// 주의: service가 막혀 있으면 queue front가 그대로 남고 다음 tick으로 넘어간다.
static void CanModule_SubmitPending(CanModule *module, uint32_t now_ms)
{
    CanModuleRequest request;
    uint8_t          submitted;
    uint8_t          send_ok;

    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    submitted = 0U;
    while (submitted < module->max_submit_per_tick)
    {
        // front를 먼저 peek한 뒤 실제 하위 전송 성공 때만 pop한다.
        if (InfraQueue_Peek(&module->request_queue, &request) != INFRA_STATUS_OK)
        {
            break;
        }

        send_ok = 0U;
        switch (request.kind)
        {
            case CAN_MODULE_REQUEST_COMMAND:
                send_ok = CanService_SendCommand(&module->service,
                                                 request.target_node_id,
                                                 request.code0,
                                                 request.code1,
                                                 request.code2,
                                                 request.need_response);
                break;

            case CAN_MODULE_REQUEST_RESPONSE:
                send_ok = CanService_SendResponse(&module->service,
                                                  request.target_node_id,
                                                  request.code0,
                                                  request.code1,
                                                  request.code2);
                break;

            case CAN_MODULE_REQUEST_EVENT:
                send_ok = CanService_SendEvent(&module->service,
                                               request.target_node_id,
                                               request.code0,
                                               request.code1,
                                               request.code2);
                break;

            case CAN_MODULE_REQUEST_TEXT:
                send_ok = CanService_SendText(&module->service,
                                              request.target_node_id,
                                              CAN_TEXT_USER,
                                              request.text);
                break;

            default:
                send_ok = 1U;
                break;
        }

        if (send_ok == 0U)
        {
            break;
        }

        (void)InfraQueue_Pop(&module->request_queue, &request);
        submitted++;
    }

    if (submitted != 0U)
    {
        // 이번 tick에 새 frame이 들어갔으면 즉시 한 번 더 transport를 돌려 지연을 줄인다.
        CanService_FlushTx(&module->service, now_ms);
        module->last_activity = 1U;
    }
}

// app 계층용 CAN module과 request queue를 초기화한다.
// 내부 CanService 객체도 여기서 함께 생성해 두어,
// 이후 AppCore task가 추가 setup 없이 작업을 제출할 수 있게 한다.
InfraStatus CanModule_Init(CanModule *module, const CanModuleConfig *config)
{
    if ((module == NULL) || (config == NULL))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(module, 0, sizeof(*module));
    module->local_node_id = config->local_node_id;
    module->default_target_node_id = config->default_target_node_id;
    module->max_submit_per_tick = config->max_submit_per_tick;
    if (module->max_submit_per_tick == 0U)
    {
        module->max_submit_per_tick = 2U;
    }

    if (InfraQueue_Init(&module->request_queue,
                        module->request_storage,
                        (uint16_t)sizeof(CanModuleRequest),
                        CAN_MODULE_REQUEST_QUEUE_SIZE) != INFRA_STATUS_OK)
    {
        return INFRA_STATUS_IO_ERROR;
    }

    if (CanService_Init(&module->service,
                        config->local_node_id,
                        config->default_timeout_ms) == 0U)
    {
        return INFRA_STATUS_IO_ERROR;
    }

    module->initialized = 1U;
    return INFRA_STATUS_OK;
}

// CAN service를 진행시키고 pending app 요청을 제출한다.
// 활성 역할에서 CAN 통신을 유지하기 위해,
// AppCore가 주기적으로 호출하면 되는 유일한 진입점이다.
void CanModule_Task(CanModule *module, uint32_t now_ms)
{
    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    module->last_activity = 0U;

    // 1) 먼저 transport 진행, RX decode, timeout 처리부터 수행한다.
    CanService_Task(&module->service, now_ms);

    // 2) 그 다음 app이 쌓아 둔 새 요청을 제한된 수만큼 service로 넘긴다.
    CanModule_SubmitPending(module, now_ms);
}

// app 계층에서 command 형태 CAN 요청 하나를 queue에 적재한다.
// 이 요청은 이후 task tick이 도착해,
// 하위 CAN service로 밀어 넣을 때까지 module queue에 머문다.
InfraStatus CanModule_QueueCommand(CanModule *module,
                                   uint8_t target_node_id,
                                   uint8_t command_code,
                                   uint8_t arg0,
                                   uint8_t arg1,
                                   uint8_t need_response)
{
    CanModuleRequest request;

    (void)memset(&request, 0, sizeof(request));
    request.kind = CAN_MODULE_REQUEST_COMMAND;
    request.target_node_id = target_node_id;
    request.code0 = command_code;
    request.code1 = arg0;
    request.code2 = arg1;
    request.need_response = need_response;
    return CanModule_PushRequest(module, &request);
}

// app 수준 command 처리 중 생성된 CAN response 하나를 queue에 적재한다.
// AppCore는 수신 요청이 로컬 노드의 원격 acknowledgement를 요구할 때,
// 이 함수를 사용한다.
InfraStatus CanModule_QueueResponse(CanModule *module,
                                    uint8_t target_node_id,
                                    uint8_t request_id,
                                    uint8_t result_code,
                                    uint8_t detail_code)
{
    CanModuleRequest request;

    (void)memset(&request, 0, sizeof(request));
    request.kind = CAN_MODULE_REQUEST_RESPONSE;
    request.target_node_id = target_node_id;
    request.code0 = request_id;
    request.code1 = result_code;
    request.code2 = detail_code;
    return CanModule_PushRequest(module, &request);
}

// 추후 제출할 비동기 event 메시지 하나를 queue에 적재한다.
// module은 이 경로를 통해,
// app 계층의 command/response 흐름과 event 생성 방식도 일관되게 유지한다.
InfraStatus CanModule_QueueEvent(CanModule *module,
                                 uint8_t target_node_id,
                                 uint8_t event_code,
                                 uint8_t arg0,
                                 uint8_t arg1)
{
    CanModuleRequest request;

    (void)memset(&request, 0, sizeof(request));
    request.kind = CAN_MODULE_REQUEST_EVENT;
    request.target_node_id = target_node_id;
    request.code0 = event_code;
    request.code1 = arg0;
    request.code2 = arg1;
    return CanModule_PushRequest(module, &request);
}

// CAN으로 보낼 짧은 text payload 하나를 queue에 적재한다.
// 이 경로는 추후 사람이 읽는 짧은 상태 메시지를
// CAN으로 보낼 필요가 있을 때 사용한다.
InfraStatus CanModule_QueueText(CanModule *module,
                                uint8_t target_node_id,
                                const char *text)
{
    CanModuleRequest request;

    if (text == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(&request, 0, sizeof(request));
    request.kind = CAN_MODULE_REQUEST_TEXT;
    request.target_node_id = target_node_id;
    (void)strncpy(request.text, text, CAN_TEXT_MAX_LEN);
    request.text[CAN_TEXT_MAX_LEN] = '\0';
    return CanModule_PushRequest(module, &request);
}

// service가 decode해 둔 수신 메시지 하나를 app 쪽으로 넘긴다.
// AppCore는 이 함수만 보면 되고,
// 하위 transport queue나 protocol 세부를 직접 알 필요는 없다.
uint8_t CanModule_TryPopIncoming(CanModule *module, CanMessage *out_message)
{
    if (module == NULL)
    {
        return 0U;
    }

    return CanService_PopReceivedMessage(&module->service, out_message);
}

// 완료된 request 결과 하나를 app 쪽으로 건네준다.
// AppCore는 이 함수를 통해 응답이나 timeout을 조금씩 꺼내며,
// service 내부 queue 구조를 직접 알지 않아도 된다.
uint8_t CanModule_TryPopResult(CanModule *module, CanServiceResult *out_result)
{
    if (module == NULL)
    {
        return 0U;
    }

    return CanService_PopResult(&module->service, out_result);
}

// app 진단에서 바로 읽을 수 있게 CAN 하위 계층 상태를 모은다.
// 현재는 service/transport/hw 오류와 RX/TX 누적 카운터만 노출해도
// 통신이 실제로 돌고 있는지 판단하는 데 충분하다.
void CanModule_GetDiag(const CanModule *module, CanModuleDiag *out_diag)
{
    const CanHw *hw;

    if (out_diag == NULL)
    {
        return;
    }

    (void)memset(out_diag, 0, sizeof(*out_diag));
    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    hw = &module->service.transport.hw;
    out_diag->service_last_error = module->service.last_error;
    out_diag->transport_last_error = module->service.transport.last_error;
    out_diag->hw_last_error = CanHw_GetLastError(hw);
    out_diag->hw_tx_busy = CanHw_IsTxBusy(hw);
    out_diag->hw_tx_ok_count = CanHw_GetTxOkCount(hw);
    out_diag->hw_tx_error_count = CanHw_GetTxErrorCount(hw);
    out_diag->hw_rx_ok_count = CanHw_GetRxOkCount(hw);
    out_diag->hw_rx_error_count = CanHw_GetRxErrorCount(hw);
    out_diag->hw_rx_drop_count = CanHw_GetRxDropCount(hw);
}

// 참고:
// request queue는 단순하고 가독성이 높지만,
// 오래 막힐 때 어떤 요청을 얼마나 기다릴지에 대한 정책은 아직 app/service 바깥에 더 분명히 적어 둘 여지가 있다.
