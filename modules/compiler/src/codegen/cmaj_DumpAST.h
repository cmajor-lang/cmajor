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


namespace cmaj
{

//==============================================================================
inline std::string dumpAST (AST::Object& object)
{
    struct PrintingVisitor
    {
        PrintingVisitor() : nextVisitIndex (AST::Visitor::getNextIndex()) {}

        void visitObject (AST::Object& o)
        {
            if (o.visitIndex >= nextVisitIndex)
                return;

            o.visitIndex = nextVisitIndex;

            out << o.getObjectType() << " {";

            auto props = o.getPropertyList();
            size_t numToPrint = 0;

            for (auto& p : props)
                if (! p.hasDefaultValue())
                    ++numToPrint;

            if (numToPrint != 0)
            {
                out << choc::text::CodePrinter::NewLine();
                auto indent = out.createIndent (2);

                for (auto& p : props)
                {
                    if (! p.property.hasDefaultValue())
                    {
                        out << p.name << ": ";

                        if (auto childObject = p.property.getAsObjectProperty())
                        {
                            if (auto child = childObject->getPointer())
                                visitObject (*child);
                            else
                                out << "null";
                        }
                        else
                        {
                            dumpProperty (p.property);
                        }

                        out << choc::text::CodePrinter::NewLine();
                    }
                }
            }

            out << "}" << choc::text::CodePrinter::NewLine();
        }

        void dumpProperty (const AST::Property& property)
        {
            if (auto p = property.getAsStringProperty())       { out << '"' << p->get() << '"'; return; }
            if (auto p = property.getAsIntegerProperty())      { out << std::to_string (p->get()); return; }
            if (auto p = property.getAsFloatProperty())        { out << choc::text::floatToString (p->get()); return; }
            if (auto p = property.getAsBoolProperty())         { out << (p->get() ? "true" : "false"); return; }
            if (auto p = property.getAsEnumProperty())         { out << p->getEnumString(); return; }

            if (auto p = property.getAsObjectProperty())
            {
                if (auto child = p->getPointer())
                    visitObject (*child);
                else
                    out << "null";

                return;
            }

            if (auto list = property.getAsListProperty())
            {
                out << "[";

                if (! list->empty())
                {
                    out << choc::text::CodePrinter::NewLine();
                    auto indent = out.createIndent (2);
                    bool first = true;

                    for (auto& p : list->get())
                    {
                        if (first)
                            first = false;
                        else
                            out << choc::text::CodePrinter::NewLine();

                        out << p->getPropertyType() << ' ';
                        dumpProperty (p.get());
                    }

                    out << choc::text::CodePrinter::NewLine();
                }

                out << "]";
            }
        }

        choc::text::CodePrinter out;
        const uint32_t nextVisitIndex;
    };

    PrintingVisitor printer;
    printer.visitObject (object);
    return printer.out.toString();
}

}
