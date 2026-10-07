#ifndef TEST_OUTPUT_HW_TASK_H
#define TEST_OUTPUT_HW_TASK_H
void test_enter_critical(void);
void test_exit_critical(void);
#define taskENTER_CRITICAL() test_enter_critical()
#define taskEXIT_CRITICAL() test_exit_critical()
#endif
