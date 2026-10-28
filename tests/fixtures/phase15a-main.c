/* phase15a original source; no Apple headers or libraries. */
extern int phase15a_helper(void);
__attribute__((used, noinline, visibility("default")))
int main(void) {
    return phase15a_helper() + 1;
}
