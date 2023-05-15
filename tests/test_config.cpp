#include "../sylar/config.h"
#include "../sylar/log.h"
#include <iostream>

sylar::ConfigVar<int>::ptr g_int_value_config =
    sylar::Config::Lookup( "system.port", (int) 8080, "system.port" );
sylar::ConfigVar<float>::ptr g_float_value_config =
    sylar::Config::Lookup( "system.value", (float) 10.2f, "system.value" );
sylar::ConfigVar<std::vector<int>>::ptr g_int_vec_value_config =
    sylar::Config::Lookup( "system.int_vec", std::vector<int>{ 1, 2 },
                           "system.int_vec" );
sylar::ConfigVar<std::list<int>>::ptr g_int_list_value_config =
    sylar::Config::Lookup( "system.int_list", std::list<int>{ 1, 2 },
                           "system.int_list" );
sylar::ConfigVar<std::set<int>>::ptr g_int_set_value_config =
    sylar::Config::Lookup( "system.int_set", std::set<int>{ 1, 2 },
                           "system.int_set" );
sylar::ConfigVar<std::unordered_set<int>>::ptr g_int_uset_value_config =
    sylar::Config::Lookup( "system.int_uset", std::unordered_set<int>{ 1, 2 },
                           "system.int_uset" );
sylar::ConfigVar<std::map<std::string, int>>::ptr g_int_map_value_config =
    sylar::Config::Lookup( "system.int_map",
                           std::map<std::string, int>{ { "k", 2 } },
                           "system.int_map" );
sylar::ConfigVar<std::unordered_map<std::string, int>>::ptr
    g_int_umap_value_config = sylar::Config::Lookup(
        "system.int_umap", std::unordered_map<std::string, int>{ { "k", 2 } },
        "system.int_umap" );

void test_config() {
    SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )
        << "before: " << g_int_value_config->getValue();
    SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )
        << "before: " << g_float_value_config->getValue();
    g_int_value_config->addListener(
        []( const int &old_value, const int &new_value ) {
            SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )
                << "old value = " << old_value << " new value = " << new_value;
        } );
#define XX( g_var, name, prefix )                                              \
    {                                                                          \
        auto v = g_var->getValue();                                            \
        for ( auto &i : v ) {                                                  \
            SYLAR_LOG_INFO( SYLAR_LOG_ROOT() ) << #prefix " " #name ": " << i; \
        }                                                                      \
        SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )                                     \
            << " " #prefix " " #name "yaml: " << g_var->toString();            \
    }
#define XX_M( g_var, name, prefix )                                            \
    {                                                                          \
        auto v = g_var->getValue();                                            \
        for ( auto &i : v ) {                                                  \
            SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )                                 \
                << #prefix " " #name ": {" << i.first << " - " << i.second     \
                << "}";                                                        \
        }                                                                      \
        SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )                                     \
            << " " #prefix " " #name "yaml: " << g_var->toString();            \
    }
    XX( g_int_vec_value_config, int_vec, before );
    XX( g_int_list_value_config, int_list, before );
    XX( g_int_set_value_config, int_set, before );
    XX( g_int_uset_value_config, int_uset, before );
    XX_M( g_int_map_value_config, int_map, before );
    XX_M( g_int_umap_value_config, int_umap, before );

    YAML::Node root = YAML::LoadFile( "/home/lee/C++/SYLAR/test.yaml" );
    sylar::Config::LoadFromYaml( root );

    XX( g_int_vec_value_config, int_vec, after );
    XX( g_int_list_value_config, int_list, after );
    XX( g_int_set_value_config, int_set, after );
    XX( g_int_uset_value_config, int_uset, after );
    XX_M( g_int_map_value_config, int_map, after );
    XX_M( g_int_umap_value_config, int_umap, after );

    SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )
        << "after: " << g_int_value_config->getValue();
    SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )
        << "after: " << g_float_value_config->getValue();
}

class Person {
  public:
    std::string m_name;
    int         m_age = 0;
    bool        m_sex = 0;
    std::string toString() const {
        std::stringstream ss;
        ss << "{Person name=" << m_name << " age=" << m_age << " sex=" << m_sex
           << "}";
        return ss.str();
    }

    bool operator==( const Person &oth ) const {
        return m_name == oth.m_name && m_age == oth.m_age && m_sex == oth.m_sex;
    }

  private:
};

namespace sylar {
template <> class LexicalCast<std::string, Person> {
  public:
    Person operator()( const std::string &v ) {
        YAML::Node        node = YAML::Load( v );
        Person            p;
        std::stringstream ss;
        p.m_name = node[ "name" ].as<std::string>();
        p.m_age  = node[ "age" ].as<int>();
        p.m_sex  = node[ "sex" ].as<bool>();
        return p;
    }
};
template <> class LexicalCast<Person, std::string> {
  public:
    std::string operator()( const Person &p ) {
        YAML::Node node;
        node[ "name" ] = p.m_name;
        node[ "age" ]  = p.m_age;
        node[ "sex" ]  = p.m_sex;
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};
} // namespace sylar

sylar::ConfigVar<Person>::ptr g_person =
    sylar::Config::Lookup( "class.person", Person(), "system person" );
sylar::ConfigVar<std::map<std::string, Person>>::ptr g_person_map =
    sylar::Config::Lookup( "class.person_map", std::map<std::string, Person>(),
                           "system person map" );
sylar::ConfigVar<std::map<std::string, std::vector<Person>>>::ptr
    g_person_vec_map =
        sylar::Config::Lookup( "class.person_vec_map",
                               std::map<std::string, std::vector<Person>>(),
                               "system person vec map" );
void test_class() {
    SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )
        << "before: " << g_person->getValue().toString() << " - "
        << g_person->toString();
#define XX_PM( g_var, prefix )                                                 \
    {                                                                          \
        auto m = g_var->getValue();                                            \
        for ( auto &i : m ) {                                                  \
            SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )                                 \
                << "before: " << prefix << ": " << i.first << " - "            \
                << i.second.toString();                                        \
        }                                                                      \
        SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )                                     \
            << prefix << ": size= " << m.size();                               \
    }

    g_person->addListener(
        []( const Person &old_value, const Person &new_value ) {
            SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )
                << "old value=" << old_value.toString()
                << " new value=" << new_value.toString();
        } );

    XX_PM( g_person_map, "class.person_map before" );

    SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )
        << "before: " << g_person_vec_map->toString();

    YAML::Node root = YAML::LoadFile( "/home/lee/C++/SYLAR/test.yaml" );
    sylar::Config::LoadFromYaml( root );

    SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )
        << "after: " << g_person_vec_map->toString();

    XX_PM( g_person_map, "class.person_map after" );

    SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )
        << "after: " << g_person->getValue().toString() << " - "
        << g_person->toString();
}

void test_log() {
    static sylar::Logger::ptr system_log = SYLAR_LOG_NAME( "system" );
    SYLAR_LOG_INFO( system_log ) << "hello system" << std::endl;
    std::cout << sylar::LoggerMgr::GetInstance()->toYamlString() << std::endl;
    std::cout << "==============" << std::endl;

    YAML::Node root = YAML::LoadFile( "/home/lee/C++/SYLAR/test.yaml" );
    sylar::Config::LoadFromYaml( root );

    std::cout << "==============" << std::endl;
    std::cout << sylar::LoggerMgr::GetInstance()->toYamlString() << std::endl;
    std::cout << "==============" << std::endl;
    std::cout << root << std::endl;
}
int main() {
    // test_config();
    // test_class();
    test_log();

    sylar::Config::Visit( []( sylar::ConfigVarBase::ptr var ) {
        SYLAR_LOG_INFO( SYLAR_LOG_ROOT() )
            << "thread=" << var->getName()
            << " description=" << var->getDescription()
            << " typename=" << var->getTypeName()
            << " value=" << var->toString();
    } );
    return 0;
}