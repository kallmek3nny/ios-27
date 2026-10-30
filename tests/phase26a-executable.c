#define JOIN0(a,b) a##b
#define JOIN(a,b) JOIN0(a,b)
extern int JOIN(phase26a_case_,PHASE26A_CASE)(void);
/* Linked MH_EXECUTE, not a synthetic opcode fixture. */
int main(void) { return JOIN(phase26a_case_,PHASE26A_CASE)(); }
