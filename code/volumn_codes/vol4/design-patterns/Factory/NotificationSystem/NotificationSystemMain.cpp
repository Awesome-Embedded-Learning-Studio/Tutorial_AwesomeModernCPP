#include "NotificationSystem.h"

int main() {
    NocificationCreator creator = NocificationCreator();
    auto email = creator.notification_creator("Email");
    email->send_message("Welcome to our platform!");

    auto sms = creator.notification_creator("SMS");
    sms->send_message("Welcome to our platform!");

    auto push = creator.notification_creator("Push");
    push->send_message("Hey, New Message here!");

    return 0;
}
