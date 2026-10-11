#include <snprocess/snprocess.h>
#include <stdio.h>

int main(void) {
    if (sn_process_get_current_id() <= 0) {
        printf("FAIL: sn_process_get_current_id\n");
        return 1;
    }

    printf("ALL TESTS PASSED\n");
    return 0;
}
