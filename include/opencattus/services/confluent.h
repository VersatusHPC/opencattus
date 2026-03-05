#ifndef OPENCATTUS_CONFLUENT_H_
#define OPENCATTUS_CONFLUENT_H_

#include <opencattus/services/provisioner.h>

namespace opencattus::services {

class Confluent final : public Provisioner<Confluent> {
public:
    static void install();
};

}

#endif
