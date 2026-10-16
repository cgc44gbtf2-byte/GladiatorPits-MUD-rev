#ifndef GLAD_ACCOUNTS_H
#define GLAD_ACCOUNTS_H

#include <stdint.h>

#define ACCOUNT_NAME_MAX 32
#define ACCOUNT_PASSWORD_MAX 128

typedef enum {
   ROLE_NONE=-1, ROLE_PLAYER, ROLE_BUILDER, ROLE_MODERATOR,
   ROLE_ADMINISTRATOR, ROLE_OWNER
} account_role;

typedef enum {
   PERM_PUBLIC=0, PERM_PLAY=1, PERM_DASHBOARD=2, PERM_WORLD_EDIT=4,
   PERM_PLAYER_MODERATE=8, PERM_PLAYER_EDIT=16, PERM_SHUTDOWN=32,
   PERM_ROLES_MANAGE=64
} account_permission;

typedef struct {
   int64_t id, epoch;
} account_session;

int AccountsOpen(const char *path);
void AccountsClose(void);
int AccountNameValid(const char *name);
int AccountPasswordValid(const char *password);
const char *AccountRoleName(account_role role);
account_role AccountRoleParse(const char *name);
int AccountRoleCan(account_role role, account_permission permission);
account_role AccountSessionRole(account_session session);
int AccountRegister(const char *name, const char *password, account_session *session);
int AccountLogin(const char *name, const char *password, account_session *session);
int AccountPasswordChange(account_session *session, const char *old_password,
   const char *new_password);
int AccountSetRole(const char *name, account_role role);
int AccountSetDisabled(const char *name, int disabled);
int AccountCharacter(account_session session, const char *name, int existing_file);
int AccountLinkLegacy(const char *account, const char *character);

#endif
