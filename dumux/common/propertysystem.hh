/*****************************************************************************
 *   Copyright (C) 2009-2011 by Andreas Lauser                               *
 *   Institute of Hydraulic Engineering                                      *
 *   University of Stuttgart, Germany                                        *
 *   email: <givenname>.<name>@iws.uni-stuttgart.de                          *
 *                                                                           *
 *   This program is free software: you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation, either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 *****************************************************************************/
/*!
 * \file
 * \brief Provides the magic behind the DuMuX property system.
 *
 * \defgroup Properties Property System
 *
 * Properties allow to associate arbitrary data types to
 * identifiers. A property is always defined on a pair (TypeTag,
 * PropertyTag) where TypeTag is the identifier for the object the
 * property is defined for and PropertyTag is an unique identifier of
 * the property.
 *
 * Type tags are hierarchic and inherit properties defined on their
 * ancesters. At each level, properties defined on lower levels can be
 * overwritten or even made undefined. It is also possible to define
 * defaults for properties if it makes sense.
 *
 * Properties may make use other properties for the respective type
 * tag and these properties can also be defined on an arbitrary level
 * of the hierarchy.
 */
#ifndef DUMUX_PROPERTIES_HH
#define DUMUX_PROPERTIES_HH

// For is_base_of
#include <boost/type_traits.hpp>

// Integral Constant Expressions

// string formating
#include <boost/format.hpp>

#include <dune/common/classname.hh>

#include <map>
#include <set>
#include <list>
#include <string>
#include <iostream>
#include <boost/lexical_cast.hpp>

#include <string.h>

namespace Dumux
{
namespace Properties
{
#if !defined NO_PROPERTY_INTROSPECTION
//! Internal macro which is only required if the property introspection is enabled
#define PROP_INFO_(EffTypeTagName, PropKind, PropTagName, ...)    \
    template <>                                                         \
    struct PropertyInfo<TTAG(EffTypeTagName), PTAG(PropTagName)>        \
    {                                                                   \
    static int init() {                                                 \
        PropertyRegistryKey key(                                        \
            /*effTypeTagName=*/ Dune::className<TTAG(EffTypeTagName)>(), \
            /*kind=*/PropKind,                                          \
            /*name=*/#PropTagName,                                      \
            /*value=*/#__VA_ARGS__,                                      \
            /*file=*/__FILE__,                                          \
            /*line=*/__LINE__);                                         \
        PropertyRegistry::addKey(key);                                  \
        return 0;                                                       \
    };                                                                  \
    static int foo;                                                     \
    };                                                                  \
int PropertyInfo<TTAG(EffTypeTagName), PTAG(PropTagName)>::foo =        \
    PropertyInfo<TTAG(EffTypeTagName), PTAG(PropTagName)>::init();

#define FA_TTAG_(TypeTagName, ...) TTAG(TypeTagName)

//! Internal macro which is only required if the property introspection is enabled
#define TTAG_INFO_(...)                                                 \
    template <>                                                         \
    struct TypeTagInfo<FA_TTAG_(__VA_ARGS__)>                           \
    {                                                                   \
        static int init() {                                             \
        TypeTagRegistry::addChildren<__VA_ARGS__ >();                   \
        return 0;                                                       \
    };                                                                  \
    static int foo;                                                     \
    };                                                                  \
int TypeTagInfo<FA_TTAG_(__VA_ARGS__)>::foo =                           \
    TypeTagInfo<FA_TTAG_(__VA_ARGS__)>::init();

#else
//! Internal macro which is only required if the property introspection is enabled
//!
//! Don't do anything if introspection is disabled
#define PROP_INFO_(EffTypeTagName, PropKind, PropTagName, ...)
#define TTAG_INFO_(EffTypeTagName, ...)
#endif

// some macros for simplification

/*!
 * \brief Makes a type out of a type tag name
 */
#define TTAG(TypeTagName) ::Dumux::Properties::TTag::TypeTagName

/*!
 * \brief Makes a type out of a property tag name
 */
#define PTAG(PropTagName) ::Dumux::Properties::PTag::PropTagName

/*!
 * \brief Define a new type tag.
 *
 * A type tag can inherit the properties defined on up to five parent
 * type tags. Examples:
 *
 * // The type tag doesn't inherit any properties from other type tags
 * NEW_TYPE_TAG(FooTypeTag);
 *
 * // BarTypeTag inherits all properties from FooTypeTag
 * NEW_TYPE_TAG(BarTypeTag, INHERITS_FROM(FooTypeTag));
 *
 * // FooBarTypeTag inherits the properties of FooTypeTag as well as
 * // those of BarTypeTag. Properties defined on BarTypeTag have
 * // preceedence over those defined for FooTypeTag:
 * NEW_TYPE_TAG(FooBarTypeTag, INHERITS_FROM(FooTypeTag, BarTypeTag));
 */
#define NEW_TYPE_TAG(TypeTagName, ...)                              \
    namespace TTag {                                                \
    struct TypeTagName : public TypeTag<TypeTagName, ##__VA_ARGS__> \
    { };                                                            \
    TTAG_INFO_(TypeTagName, ##__VA_ARGS__)                          \
    }                                                               \
    extern int semicolonHack_

/*!
 * \brief Syntactic sugar for NEW_TYPE_TAG.
 *
 * See the documentation for NEW_TYPE_TAG.
 */
#define INHERITS_FROM(...) __VA_ARGS__

/*!
 * \brief Define a property tag.
 *
 * A property tag is the unique identifier for a property. It may only
 * be declared once in your program. There is also no hierarchy of
 * property tags as for type tags.
 *
 * Examples:
 *
 * NEW_PROP_TAG(blubbPropTag);
 * NEW_PROP_TAG(blabbPropTag);
 */
#define NEW_PROP_TAG(PTagName) \
    namespace PTag {                                       \
    struct PTagName; } extern int semicolonHack_

/*!
 * \brief Set the default for a property.
 *
 * SET_PROP_DEFAULT works exactly like SET_PROP, except that it does
 * not require an effective type tag. Defaults are used whenever a
 * property was not explicitly set or explicitly unset for a type tag.
 *
 * Example:
 *
 * // set a default for the blabbPropTag property tag
 * SET_PROP_DEFAULT(blabbPropTag)
 * {
 *    static const int value = 3;
 * };
 */
#define SET_PROP_DEFAULT(PropTagName) \
    template <class TypeTag>                                            \
    struct DefaultProperty<TypeTag, PTAG(PropTagName)>;                 \
    PROP_INFO_(__Default,                                               \
               /*kind=*/"<opaque>",                                     \
               PropTagName,                                             \
               /*value=*/"<opaque>")                                    \
    template <class TypeTag>                                            \
    struct DefaultProperty<TypeTag, PTAG(PropTagName) >

//! Internal macro
#define SET_PROP_(EffTypeTagName, PropKind, PropTagName, ...)       \
    template <class TypeTag>                                        \
    struct Property<TypeTag,                                        \
                    TTAG(EffTypeTagName),                           \
                    PTAG(PropTagName)>;                             \
    PROP_INFO_(EffTypeTagName,                                      \
               /*kind=*/PropKind,                                   \
               PropTagName,                                         \
               /*value=*/__VA_ARGS__)                               \
    template <class TypeTag>                                        \
    struct Property<TypeTag, \
                    TTAG(EffTypeTagName), \
                    PTAG(PropTagName) >

/*!
 * \brief Set a property for a specific type tag.
 *
 * After this macro, you must to specify a complete body of a class
 * template, including the trailing semicolon. If you need to retrieve
 * another property within the class body, you can use TypeTag as the
 * argument for the type tag for the GET_PROP macro.
 *
 * Example:
 *
 * SET_PROP(FooTypeTag, blubbPropTag)
 * {
 *    static int value = 10;
 *    static int calculate(int arg)
 *    { calculateInternal_(arg); }
 *
 * private:
 *    // retrieve the blabbProp property for the real TypeTag the
 *    // property is defined on. Note that blabbProb does not need to
 *    // be defined on FooTypeTag, but can also be defined for some
 *    // derived type tag.
 *    typedef typename GET_PROP(TypeTag, blabbProp) blabb;
 *
 *    static int calculateInternal_(int arg)
 *    { return arg * blabb::value; };
 * };
 */
#define SET_PROP(EffTypeTagName, PropTagName) \
    template <class TypeTag>                                    \
    struct Property<TypeTag,                                    \
                    TTAG(EffTypeTagName),                       \
                    PTAG(PropTagName)>;                         \
    PROP_INFO_(EffTypeTagName,                                  \
               /*kind=*/"opaque",                               \
               PropTagName,                                     \
               /*value=*/"<opaque>")                            \
    template <class TypeTag>                                    \
    struct Property<TypeTag, \
                    TTAG(EffTypeTagName), \
                    PTAG(PropTagName) >

/*!
 * \brief Explicitly unset a property for a type tag.
 *
 * This means that the property will not be inherited from the type
 * tag's parents and that no default will be used.
 *
 * Example:
 *
 * // make the blabbPropTag property undefined for the BarTypeTag.
 * UNSET_PROP(BarTypeTag, blabbPropTag);
 */
#define UNSET_PROP(EffTypeTagName, PropTagName)                 \
    template <>                                                 \
    struct PropertyUnset<TTAG(EffTypeTagName),                  \
                         PTAG(PropTagName) >;                   \
    PROP_INFO_(EffTypeTagName,                                  \
               /*kind=*/"withdraw",                             \
               PropTagName,                                     \
               /*value=*/<none>)                                \
    template <>                                                 \
    struct PropertyUnset<TTAG(EffTypeTagName),                  \
                         PTAG(PropTagName) >                    \
        : public PropertyExplicitlyUnset                        \
        {}

/*!
 * \brief Set a property to a simple constant integer value.
 *
 * The constant can be accessed by the 'value' attribute.
 */
#define SET_INT_PROP(EffTypeTagName, PropTagName, Value, ...)   \
    SET_PROP_(EffTypeTagName,                                   \
              /*kind=*/"int   ",                                \
              PropTagName,                                      \
              /*value=*/Value, ##__VA_ARGS__)                   \
    {                                                           \
        typedef int type;                                       \
        static constexpr int value = Value, ##__VA_ARGS__;      \
    }

/*!
 * \brief Set a property to a simple constant boolean value.
 *
 * The constant can be accessed by the 'value' attribute.
 */
#define SET_BOOL_PROP(EffTypeTagName, PropTagName, Value, ...)  \
    SET_PROP_(EffTypeTagName,                                   \
              /*kind=*/"bool  ",                                \
              PropTagName,                                      \
              /*value=*/Value, ##__VA_ARGS__)                   \
    {                                                           \
        typedef bool type;                                      \
        static constexpr bool value = Value, ##__VA_ARGS__;     \
    }

/*!
 * \brief Set a property which defines a type.
 *
 * The type can be accessed by the 'type' attribute.
 */
#define SET_TYPE_PROP(EffTypeTagName, PropTagName, Type, ...)   \
    SET_PROP_(EffTypeTagName,                                   \
              /*kind=*/"type  ",                                \
              PropTagName,                                      \
              /*value=*/Type, __VA_ARGS__)                      \
    {                                                           \
        typedef Type, ##__VA_ARGS__ type;                       \
    }

/*!
 * \brief Set a property to a simple constant scalar value.
 *
 * The constant can be accessed by the 'value' attribute. In order to
 * use this macro, the property tag "Scalar" needs to be defined for
 * the real type tag.
 */
#define SET_SCALAR_PROP(EffTypeTagName, PropTagName, ...)               \
    SET_PROP_(EffTypeTagName,                                           \
              /*kind=*/"scalar",                                        \
              PropTagName,                                              \
              /*value=*/__VA_ARGS__)                                    \
    {                                                                   \
        typedef typename GET_PROP_TYPE(TypeTag, PTAG(Scalar)) Scalar;   \
    public:                                                             \
        typedef Scalar type;                                            \
        static constexpr Scalar value = __VA_ARGS__;                    \
    }

/*!
 * \brief Set a property to a simple constant string value.
 *
 * The constant can be accessed by the 'value' attribute and is of
 * type std::string.
 */
#define SET_STRING_PROP(EffTypeTagName, PropTagName, ...)               \
    SET_PROP_(EffTypeTagName,                                           \
              /*kind=*/"string",                                        \
              PropTagName,                                              \
              /*value=*/__VA_ARGS__)                                    \
    {                                                                   \
    public:                                                             \
        typedef std::string type;                                       \
        static const std::string value;                                 \
    };                                                                  \
    template <class TypeTag>                                            \
    const std::string Property<TypeTag, TTAG(EffTypeTagName), PTAG(PropTagName)>::value(__VA_ARGS__);

/*!
 * \brief Get the property for a type tag.
 *
 * If you use GET_PROP within a template and want to refer to some
 * type (including the property itself), GET_PROP must be preceeded by
 * the 'typename' keyword.
 */
#define GET_PROP(TypeTag, PropTag) \
    ::Dumux::Properties::GetProperty<TypeTag, PropTag>::p

/*!
 * \brief Access the 'value' attribute of a property for a type tag.
 *
 * This is just for convenience and equivalent to GET_PROP(TypeTag,
 * PropTag) :: value.  If the property doesn't have an attribute named
 * 'value', this yields a compiler error.
 */
#define GET_PROP_VALUE(TypeTag, PropTag) \
    ::Dumux::Properties::GetProperty<TypeTag, PropTag>::p::value

/*!
 * \brief Access the 'type' attribute of a property for a type tag.
 *
 * This is just for convenience and equivalent to GET_PROP(TypeTag,
 * PropTag) :: type.  If the property doesn't have an attribute named
 * 'type', this yields a compiler error. Also, if you use this macro
 * within a template, it must be preceeded by the 'typename' keyword.
 */
#define GET_PROP_TYPE(TypeTag, PropTag) \
    ::Dumux::Properties::GetProperty<TypeTag, PropTag>::p::type

#if !defined NO_PROPERTY_INTROSPECTION
/*!
 * \brief Return a human readable diagnostic message how exactly a
 *        property was defined.
 *
 * This is only enabled if the NO_PROPERTY_INTROSPECTION macro is not
 * defined.
 *
 * Example:
 *
 * int main()
 * {
 *    std::cout << PROP_DIAGNOSTIC(FooBarTypeTag, blabbPropTag) << "\n";
 * };
 */
#define PROP_DIAGNOSTIC(TypeTag, PropTagName) \
    ::Dumux::Properties::getDiagnostic<TypeTag>(#PropTagName)

#else
/*!
 * \brief Return a human readable diagnostic message how exactly a
 *        property was defined.
 *
 * This is only enabled if the NO_PROPERTY_INTROSPECTION macro is not
 * defined.
 *
 * Example:
 *
 * int main()
 * {
 *    std::cout << PROP_DIAGNOSTIC(FooBarTypeTag, blabbPropTag) << "\n";
 * };
 */
#define PROP_DIAGNOSTIC(TypeTag, PropTag) "Property introspection disabled by NO_PROPERTY_INTROSPECTION"
#endif


//////////////////////////////////////////////
// some serious template kung fu. Don't look at it too closely, it
// might damage your brain!
//////////////////////////////////////////////

//! \cond 0

namespace PTag {}
namespace TTag {}

#if !defined NO_PROPERTY_INTROSPECTION

namespace TTag
{
template <class EffTypeTag>
struct TypeTagInfo
{};
}

template <class EffTypeTagName, class PropTagName>
struct PropertyInfo
{};
class PropertyRegistryKey
{
public:
    PropertyRegistryKey()
    {};

    PropertyRegistryKey(const std::string &effTypeTagName,
                        const std::string &propertyKind,
                        const std::string &propertyName,
                        const std::string &propertyValue,
                        const std::string &fileDefined,
                        int lineDefined)
        : effTypeTagName_(effTypeTagName)
        , propertyKind_(propertyKind)
        , propertyName_(propertyName)
        , propertyValue_(propertyValue)
        , fileDefined_(fileDefined)
        , lineDefined_(lineDefined)
    {
    };

    // copy constructor
    PropertyRegistryKey(const PropertyRegistryKey &v)
        : effTypeTagName_(v.effTypeTagName_)
        , propertyKind_(v.propertyKind_)
        , propertyName_(v.propertyName_)
        , propertyValue_(v.propertyValue_)
        , fileDefined_(v.fileDefined_)
        , lineDefined_(v.lineDefined_)
    {};

    const std::string &effTypeTagName() const
    { return effTypeTagName_; }
    const std::string &propertyKind() const
    { return propertyKind_; }
    const std::string &propertyName() const
    { return propertyName_; }
    const std::string &propertyValue() const 
    { return propertyValue_; }
    const std::string &fileDefined() const
    { return fileDefined_; }
    int lineDefined() const
    { return lineDefined_; }

private:
    std::string effTypeTagName_;
    std::string propertyKind_;
    std::string propertyName_;
    std::string propertyValue_;
    std::string fileDefined_;
    int lineDefined_;
};

class PropertyRegistry
{
public:
    typedef std::map<std::string, PropertyRegistryKey> KeyList;
    typedef std::map<std::string, KeyList> KeyListMap;

    static void addKey(const PropertyRegistryKey &key)
    {
        keys_[key.effTypeTagName()][key.propertyName()] = key;
    }

    static const PropertyRegistryKey &getKey(const std::string &effTypeTagName,
                                             const std::string &propertyName)
    {
        return keys_[effTypeTagName][propertyName];
    };

    static const KeyList &getKeys(const std::string &effTypeTagName)
    {
        return keys_[effTypeTagName];
    };

private:
    static KeyListMap keys_;
};
PropertyRegistry::KeyListMap PropertyRegistry::keys_;

class TypeTagRegistry
{
public:
    typedef std::list<std::string> ChildrenList;
    typedef std::map<std::string, ChildrenList> ChildrenListMap;
    
    template <class TypeTag,
              class Child1 = void, 
              class Child2 = void, 
              class Child3 = void, 
              class Child4 = void, 
              class Child5 = void>
    static void addChildren()
    {
        std::string typeTagName = Dune::className<TypeTag>();
        if (typeid(Child1) != typeid(void))
            keys_[typeTagName].push_front(Dune::className<Child1>());
        if (typeid(Child2) != typeid(void))
            keys_[typeTagName].push_front(Dune::className<Child2>());
        if (typeid(Child3) != typeid(void))
            keys_[typeTagName].push_front(Dune::className<Child3>());
        if (typeid(Child4) != typeid(void))
            keys_[typeTagName].push_front(Dune::className<Child4>());
        if (typeid(Child5) != typeid(void))
            keys_[typeTagName].push_front(Dune::className<Child5>());
    };

    static const ChildrenList &children(const std::string &typeTagName)
    {
        return keys_[typeTagName];
    };
    
private:
    static ChildrenListMap keys_;
};

TypeTagRegistry::ChildrenListMap TypeTagRegistry::keys_;
#endif // !defined NO_PROPERTY_INTROSPECTION

using namespace boost::type_traits;
using boost::is_void;
using boost::is_base_of;

//! \internal
class PropertyUndefined {};
//! \internal
class PropertyExplicitlyUnset {};

//! \internal
template <class RealTypeTag,
          class EffectiveTypeTag,
          class PropertyTag>
struct Property : public PropertyUndefined
{
};

//! \internal
template <class EffectiveTypeTag,
          class PropertyTag>
struct PropertyUnset : public PropertyUndefined
{
};

//! \internal
template <class RealTypeTag,
          class PropertyTag>
struct DefaultProperty : public PropertyUndefined
{
};

//! \internal
template <class Tree, class PropertyTag>
struct propertyExplicitlyUnset
{
    const static bool value =
        is_base_of<PropertyExplicitlyUnset,
                   PropertyUnset<typename Tree::SelfType,
                                 PropertyTag>
                   >::value;
};

//! \internal
template <class Tree, class PropertyTag>
class propertyExplicitlyUnsetOnTree
{
    static const bool explicitlyUnset = propertyExplicitlyUnset<Tree, PropertyTag>::value;

    static const bool isLeaf = ice_and<is_void<typename Tree::Child1>::value,
                                       is_void<typename Tree::Child2>::value,
                                       is_void<typename Tree::Child3>::value,
                                       is_void<typename Tree::Child4>::value,
                                       is_void<typename Tree::Child5>::value >::value;

public:
    static const bool value =
        ice_or<explicitlyUnset,
               ice_and<ice_not<isLeaf>::value,
                       propertyExplicitlyUnsetOnTree<typename Tree::Child1, PropertyTag>::value,
                       propertyExplicitlyUnsetOnTree<typename Tree::Child2, PropertyTag>::value,
                       propertyExplicitlyUnsetOnTree<typename Tree::Child3, PropertyTag>::value,
                       propertyExplicitlyUnsetOnTree<typename Tree::Child4, PropertyTag>::value,
                       propertyExplicitlyUnsetOnTree<typename Tree::Child5, PropertyTag>::value
                       >::value
               >::value;
};

//! \internal
template <class PropertyTag>
struct propertyExplicitlyUnsetOnTree<void, PropertyTag>
{
    const static bool value = boost::true_type::value;
};

//! \internal
template <class RealTypeTag, class Tree, class PropertyTag>
struct propertyDefinedOnSelf
{
    const static bool value =
        ice_not<is_base_of<PropertyUndefined,
                           Property<RealTypeTag,
                                    typename Tree::SelfType,
                                    PropertyTag>
                           >::value
                           >::value;
};

//! \internal
template <class RealTypeTag, class Tree, class PropertyTag>
class propertyDefinedOnTree
{
    static const bool notExplicitlyUnset =
        ice_not<propertyExplicitlyUnsetOnTree<Tree,
                                              PropertyTag>::value >::value;

public:
    static const bool value =
        ice_and<notExplicitlyUnset,
                ice_or<propertyDefinedOnSelf<RealTypeTag, Tree, PropertyTag>::value,
                       propertyDefinedOnTree<RealTypeTag, typename Tree::Child1, PropertyTag>::value,
                       propertyDefinedOnTree<RealTypeTag, typename Tree::Child2, PropertyTag>::value,
                       propertyDefinedOnTree<RealTypeTag, typename Tree::Child3, PropertyTag>::value,
                       propertyDefinedOnTree<RealTypeTag, typename Tree::Child4, PropertyTag>::value,
                       propertyDefinedOnTree<RealTypeTag, typename Tree::Child5, PropertyTag>::value
                       >::value >::value;
};

//! \internal
template <class RealTypeTag, class PropertyTag>
class propertyDefinedOnTree<RealTypeTag, void, PropertyTag>
{
public:
    static const bool value = boost::false_type::value;
};

//! \internal
template <class RealTypeTag, class PropertyTag>
struct defaultPropertyDefined
{
    const static bool value =
        ice_not<is_base_of<PropertyUndefined,
                           DefaultProperty<RealTypeTag,
                                           PropertyTag>
                           >::value
                           >::value;
};

//! \internal
template <class RealTypeTag, class Tree, class PropertyTag>
class defaultPropertyDefinedOnTree
{
    static const bool isLeaf = ice_and<is_void<typename Tree::Child1>::value,
                                       is_void<typename Tree::Child2>::value,
                                       is_void<typename Tree::Child3>::value,
                                       is_void<typename Tree::Child4>::value,
                                       is_void<typename Tree::Child5>::value >::value;

    static const bool explicitlyUnset =
        propertyExplicitlyUnsetOnTree<Tree, PropertyTag>::value;

public:
    static const bool value =
        ice_and<ice_not<explicitlyUnset>::value,
                ice_or<ice_and<isLeaf,defaultPropertyDefined<RealTypeTag, PropertyTag>::value >::value,
                       defaultPropertyDefinedOnTree<RealTypeTag,typename Tree::Child1, PropertyTag>::value,
                       defaultPropertyDefinedOnTree<RealTypeTag,typename Tree::Child2, PropertyTag>::value,
                       defaultPropertyDefinedOnTree<RealTypeTag,typename Tree::Child3, PropertyTag>::value,
                       defaultPropertyDefinedOnTree<RealTypeTag,typename Tree::Child4, PropertyTag>::value,
                       defaultPropertyDefinedOnTree<RealTypeTag,typename Tree::Child5, PropertyTag>::value
                       >::value >::value;
};

//! \internal
template <class RealTypeTag, class PropertyTag>
struct defaultPropertyDefinedOnTree<RealTypeTag,void, PropertyTag>
{
    static const bool value = boost::false_type::value;
};

//! \internal
template <class RealTypeTag, class Tree, class PropertyTag>
class propertyDefined
{
public:
    static const bool onSelf = propertyDefinedOnSelf<RealTypeTag,Tree,PropertyTag>::value;

    static const bool onChild1 = propertyDefinedOnTree<RealTypeTag,typename Tree::Child1,PropertyTag>::value;
    static const bool onChild2 = propertyDefinedOnTree<RealTypeTag,typename Tree::Child2,PropertyTag>::value;
    static const bool onChild3 = propertyDefinedOnTree<RealTypeTag,typename Tree::Child3,PropertyTag>::value;
    static const bool onChild4 = propertyDefinedOnTree<RealTypeTag,typename Tree::Child4,PropertyTag>::value;
    static const bool onChild5 = propertyDefinedOnTree<RealTypeTag,typename Tree::Child5,PropertyTag>::value;

    static const bool asDefault =
        defaultPropertyDefinedOnTree<RealTypeTag, Tree,PropertyTag>::value;

    static const bool onChildren =
        ice_or<onChild1,
               onChild2,
               onChild3,
               onChild4,
               onChild5
               >::value;

    static const bool value =
        ice_or<onSelf ,
               onChildren>::value;


};

//! \internal
template <class RealTypeTag, class Tree, class PropertyTag>
class propertyTagIndex
{
    typedef propertyDefined<RealTypeTag, Tree, PropertyTag> definedWhere;

public:
    static const int value =
        definedWhere::onSelf ? 0 :
        ( definedWhere::onChild5 ? 5 :
          ( definedWhere::onChild4 ? 4 :
            ( definedWhere::onChild3 ? 3 :
              ( definedWhere::onChild2 ? 2 :
                ( definedWhere::onChild1 ? 1 :
                  ( definedWhere::asDefault ? -1 :
                    -1000))))));
};


//! \internal
template <class SelfT,
          class Child1T = void,
          class Child2T = void,
          class Child3T = void,
          class Child4T = void,
          class Child5T = void>
class TypeTag
{
public:
    typedef SelfT SelfType;

    typedef Child1T Child1;
    typedef Child2T Child2;
    typedef Child3T Child3;
    typedef Child4T Child4;
    typedef Child5T Child5;
};

NEW_TYPE_TAG(__Default);

//! \internal
template <class EffectiveTypeTag,
          class PropertyTag,
          class RealTypeTag=EffectiveTypeTag,
          int tagIndex = propertyTagIndex<RealTypeTag, EffectiveTypeTag, PropertyTag>::value >
struct GetProperty
{
};

// property not defined, but a default property is available
//! \internal
template <class TypeTag, class PropertyTag, class RealTypeTag>
struct GetProperty<TypeTag, PropertyTag, RealTypeTag, -1>
{
    typedef DefaultProperty<RealTypeTag, PropertyTag>  p;
};

// property defined on self
//! \internal
template <class TypeTag, class PropertyTag, class RealTypeTag>
struct GetProperty<TypeTag, PropertyTag, RealTypeTag, 0>
{
    typedef Property<RealTypeTag, TypeTag, PropertyTag>   p;
};

//! \internal
template <class TypeTag, class PropertyTag, class RealTypeTag>
struct GetProperty<TypeTag, PropertyTag, RealTypeTag, 1>
{
    typedef typename GetProperty<typename TypeTag::Child1, PropertyTag, RealTypeTag>::p p;
};

//! \internal
template <class TypeTag, class PropertyTag, class RealTypeTag>
struct GetProperty<TypeTag, PropertyTag, RealTypeTag, 2>
{
    typedef typename GetProperty<typename TypeTag::Child2, PropertyTag, RealTypeTag>::p p;
};

//! \internal
template <class TypeTag, class PropertyTag, class RealTypeTag>
struct GetProperty<TypeTag, PropertyTag, RealTypeTag, 3>
{
    typedef typename GetProperty<typename TypeTag::Child3, PropertyTag, RealTypeTag>::p p;
};

//! \internal
template <class TypeTag, class PropertyTag, class RealTypeTag>
struct GetProperty<TypeTag, PropertyTag, RealTypeTag, 4>
{
    typedef typename GetProperty<typename TypeTag::Child4, PropertyTag, RealTypeTag>::p p;
};

//! \internal
template <class TypeTag, class PropertyTag, class RealTypeTag>
struct GetProperty<TypeTag, PropertyTag, RealTypeTag, 5>
{
    typedef typename GetProperty<typename TypeTag::Child5, PropertyTag, RealTypeTag>::p p;
};

#if !defined NO_PROPERTY_INTROSPECTION
std::string canonicalTypeTagNameToName_(const std::string &canonicalName)
{ 
    std::string result(canonicalName);
    result.replace(0, strlen("Dumux::Properties::TTag::"), "");
    return result;
}

inline bool getDiagnostic_(const std::string &typeTagName,
                           const std::string &propTagName,
                           std::string &result,
                           const std::string indent)
{
    const PropertyRegistryKey *key = 0;

    const PropertyRegistry::KeyList &keys =
        PropertyRegistry::getKeys(typeTagName);
    PropertyRegistry::KeyList::const_iterator it = keys.begin();
    for (; it != keys.end(); ++it) {
        if (it->second.propertyName() == propTagName) {
            key = &it->second;
            break;
        };
    }
    
    if (key) {
        result = indent;
        result +=
            key->propertyKind() + " "
            + key->propertyName() + " defined on '"
            + canonicalTypeTagNameToName_(key->effTypeTagName()) + "' at "
            + key->fileDefined() + ":" + boost::lexical_cast<std::string>(key->lineDefined()) + "\n";
        return true;
    }

    // print properties defined on children
    typedef TypeTagRegistry::ChildrenList ChildrenList;
    const ChildrenList &children = TypeTagRegistry::children(typeTagName);
    ChildrenList::const_iterator ttagIt = children.begin();
    std::string newIndent = indent + "  ";
    for (; ttagIt != children.end(); ++ttagIt) {
        if (getDiagnostic_(*ttagIt, propTagName, result, newIndent)) {
            result.insert(0, indent + "Inherited from " + canonicalTypeTagNameToName_(typeTagName) + "\n");
            return true;
        }
    }

    return false;
}

template <class TypeTag>
const std::string getDiagnostic(std::string propTagName)
{
    std::string result;

    std::string TypeTagName(Dune::className<TypeTag>());

    propTagName.replace(0, strlen("PTag("), "");
    int n = propTagName.length();
    propTagName.replace(n - 1, 1, "");
    //TypeTagName.replace(0, strlen("Dumux::Properties::TTag::"), "");
    
    if (!getDiagnostic_(TypeTagName, propTagName, result, "")) {
        // check whether the property is a default property
        const PropertyRegistry::KeyList &keys =
            PropertyRegistry::getKeys(Dune::className<TTAG(__Default)>());
        PropertyRegistry::KeyList::const_iterator it = keys.begin();
        for (; it != keys.end(); ++it) {
            const PropertyRegistryKey &key = it->second;
            if (key.propertyName() != propTagName)
                continue; // property already printed
            result = "fallback " + key.propertyName()
                + " defined " + key.fileDefined()
                + ":" + boost::lexical_cast<std::string>(key.lineDefined())
                + "\n";
        };
    }


    return result;
};

//! \internal
template <class TypeTag>
void print(std::ostream &os = std::cout)
{
    std::set<std::string> printedProps;
    print_(Dune::className<TypeTag>(), os, "", printedProps);

    // print the default properties
    const PropertyRegistry::KeyList &keys =
        PropertyRegistry::getKeys(Dune::className<TTAG(__Default)>());
    PropertyRegistry::KeyList::const_iterator it = keys.begin();
    for (; it != keys.end(); ++it) {
        const PropertyRegistryKey &key = it->second;
        if (printedProps.count(key.propertyName()) > 0)
            continue; // property already printed
        os << "  default " << key.propertyName()
           << " (" << key.fileDefined()
           << ":" << key.lineDefined()
           << ")\n";
        printedProps.insert(key.propertyName());
    };
}

inline void print_(const std::string &typeTagName,
                   std::ostream &os,
                   const std::string indent,
                   std::set<std::string> &printedProperties)
{
    if (indent == "")
        os << indent << "Properties for " << canonicalTypeTagNameToName_(typeTagName) << ":";
    else
        os << indent << "Inherited from " << canonicalTypeTagNameToName_(typeTagName) << ":";
    const PropertyRegistry::KeyList &keys =
        PropertyRegistry::getKeys(typeTagName);
    PropertyRegistry::KeyList::const_iterator it = keys.begin();
    bool somethingPrinted = false;
    for (; it != keys.end(); ++it) {
        const PropertyRegistryKey &key = it->second;
        if (printedProperties.count(key.propertyName()) > 0)
            continue; // property already printed
        if (!somethingPrinted) {
            os << "\n";
            somethingPrinted = true;
        }
        os << indent << "  " 
           << key.propertyKind() << " " << key.propertyName();
        if (key.propertyKind() != "opaque")
            os << " = '" << key.propertyValue() << "'";
        os << " defined at " << key.fileDefined()
           << ":" << key.lineDefined()
           << "\n";
        printedProperties.insert(key.propertyName());
    };
    if (!somethingPrinted)
        os << " (none)\n";
    // print properties defined on children
    typedef TypeTagRegistry::ChildrenList ChildrenList;
    const ChildrenList &children = TypeTagRegistry::children(typeTagName);
    ChildrenList::const_iterator ttagIt = children.begin();
    std::string newIndent = indent + "  ";
    for (; ttagIt != children.end(); ++ttagIt) {
        print_(*ttagIt, os, newIndent, printedProperties);
    }
}
#else // !defined NO_PROPERTY_INTROSPECTION
template <class TypeTag>
void print(std::ostream &os = std::cout)
{
    std::cout <<
        "The Dumux property system was compiled with the macro\n"
        "NO_PROPERTY_INTROSPECTION defined.\n"
        "No diagnostic messages this time, sorry.\n";
}

template <class TypeTag>
const std::string getDiagnostic(std::string propTagName)
{
    std::string result;
    result =
        "The Dumux property system was compiled with the macro\n"
        "NO_PROPERTY_INTROSPECTION defined.\n"
        "No diagnostic messages this time, sorry.\n";
    return result;
};

#endif // !defined NO_PROPERTY_INTROSPECTION

//! \endcond

} // namespace Properties
} // namespace Dumux

#endif
