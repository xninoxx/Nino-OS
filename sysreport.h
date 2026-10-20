/* sysreport.h - facts about this system for bug reports and the 'bugreport' command */
#ifndef SYSREPORT_H
#define SYSREPORT_H

void sysreport_cpu(char *out, int size);                  /* CPU brand string */
void sysreport_text(char *out, int size);                 /* the whole report, several lines */

#endif /* SYSREPORT_H */
