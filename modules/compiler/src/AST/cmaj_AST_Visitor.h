//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  The Cmajor project is subject to commercial or open-source licensing.
//  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
//  visit https://cmajor.dev to learn about our commercial licence options.
//
//  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
//  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
//  DISCLAIMED.


struct Visitor
{
    Visitor (Allocator& a)
       : allocator (a),
         visitorDepth (allocator.visitorStackDepth++),
         visitorNumber (++allocator.nextVisitorNumber)
    {
        CMAJ_ASSERT (allocator.visitorStackDepth < Object::maxActiveVisitorStackDepth);
    }

    virtual ~Visitor()
    {
        --allocator.visitorStackDepth;
    }

    //==============================================================================
    virtual void visitObject (Object& o)
    {
        if (shouldVisitObject (o) && o.checkAndUpdateVisitorStatus (visitorDepth, visitorNumber))
        {
            visitStack.push_back (std::addressof (o));
            o.invokeVisitorCallback (*this);
            visitStack.pop_back();
        }
    }

    void visitProperty (Property& p)    { p.visitObjects (*this); }

    //==============================================================================
    #define CMAJ_DEFINE_CLASS_VISIT_METHOD(Class) \
        virtual void visit (Class& o) \
        { \
            o.visitObjects (*this); \
        }
    CMAJ_AST_CLASSES (CMAJ_DEFINE_CLASS_VISIT_METHOD)
    #undef CMAJ_DEFINE_CLASS_VISIT_METHOD

    #define CMAJ_DO_NOT_VISIT_CONSTANTS \
        void visit (AST::ConstantAggregate&) override {}

    // override to exclude certain types of objects from being visited
    virtual bool shouldVisitObject (Object&)   { return true; }

    Object& getPreviousObjectOnVisitStack() const
    {
        CMAJ_ASSERT (visitStack.size() > 1);
        return *visitStack[visitStack.size() - 2];
    }

    Object& findTopOfCurrentStatement() const
    {
        CMAJ_ASSERT (! visitStack.empty());
        size_t i = visitStack.size() - 1;

        while (i > 0)
        {
            auto parent = visitStack[i - 1];

            if (parent->getAsScopeBlock() != nullptr
                 || parent->getAsLoopStatement() != nullptr
                 || parent->getAsIfStatement() != nullptr)
                break;

            --i;
        }

        return *visitStack[i];
    }

    template <typename ObjectType>
    bool visitStackContains() const
    {
        for (auto o : visitStack)
            if (castTo<ObjectType> (*o))
                return true;

        return false;
    }

    template <typename ObjectType>
    ptr<ObjectType> findTopVisitedItemOfType() const
    {
        for (size_t i = visitStack.size(); i > 0; --i)
            if (auto result = castTo<ObjectType> (*visitStack[i - 1]))
                return result;

        return {};
    }

    Allocator& allocator;
    const uint32_t visitorDepth;
    const uint16_t visitorNumber;
    choc::SmallVector<Object*, 64> visitStack;
};

//==============================================================================
struct NonParameterisedObjectVisitor  : public Visitor
{
    NonParameterisedObjectVisitor (Allocator& a) : Visitor (a) {}

    bool shouldVisitObject (Object& o) override
    {
        return ! o.isGenericOrParameterised();
    }
};
