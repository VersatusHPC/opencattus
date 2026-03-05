#include <opencattus/models/cluster.h>
#include <opencattus/services/options.h>

namespace opencattus::services {
using namespace opencattus;

// Singletons that depends only in the options, the cluster model
// depends on these
void initializeSingletonsOptions(std::unique_ptr<const Options>&& opts);

void initializeSingletonsModel(
    std::unique_ptr<opencattus::models::Cluster>&& cluster,
    std::unique_ptr<const opencattus::models::AnswerFile>&& answerfile);

}
