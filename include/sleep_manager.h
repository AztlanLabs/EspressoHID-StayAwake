#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

void enterSleep(unsigned long durationMs, const char* reason);
void handleSleep();
void maybeScheduleBreak();

#endif // SLEEP_MANAGER_H
