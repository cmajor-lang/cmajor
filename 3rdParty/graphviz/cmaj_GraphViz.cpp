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

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <math.h>
#include <sys/stat.h>
#include <signal.h>
#include <set>
#include <mutex>

#include "../../modules/compiler/include/cmaj_ErrorHandling.h"
#include "choc/text/choc_OpenSourceLicenseList.h"
#include "choc/platform/choc_DisableAllWarnings.h"

namespace GraphViz
{

std::mutex lock;
std::set<void*> allocations;

void* GraphVizMalloc (size_t size)
{
    auto* result = malloc (size);
    allocations.insert (result);
    return result;
}

void* GraphVizCalloc (size_t nitems, size_t size)
{
    auto* result = calloc (nitems, size);
    allocations.insert (result);
    return result;
}

void* GraphVizRealloc (void *ptr, size_t new_size)
{
    auto* result = realloc (ptr, new_size);
    allocations.erase (ptr);
    allocations.insert (result);
    return result;
}

void GraphVizFree (void* p)
{
    free (p);
    allocations.erase (p);
}

struct AllocationTidier
{
    ~AllocationTidier()
    {
        for (auto a : allocations)
            free (a);
    }
};

static AllocationTidier tidyAllocations;

#define malloc GraphVizMalloc
#define calloc GraphVizCalloc
#define realloc GraphVizRealloc
#define free GraphVizFree

#undef TRANSPARENT
#undef DEBUG
#define EXTERN
#define XLABEL_INT 1

#include "./dotgen/dotinit.c"
#include "./dotgen/fastgr.c"
#include "./dotgen/flat.c"
#include "./dotgen/dotsplines.c"

#undef FUDGE

#include "./dotgen/mincross.c"

#undef MARK

#include "./dotgen/position.c"
#include "./dotgen/rank.c"
#include "./dotgen/sameport.c"
#include "./pack/ccomps.c"

#undef MARK

#include "./pack/pack.c"

#undef CELL

#include "./ortho/fPQ.c"
#include "./ortho/maze.c"
#include "./ortho/partition.c"

#undef LENGTH

#include "./ortho/rawgraph.c"
#include "./ortho/sgraph.c"
#include "./ortho/trapezoid.c"

#undef hash
#undef KEY

#include "./expat/xmlparse.c"

#undef encoding
#undef userData
#undef atts
#undef ns

#include "./expat/xmltok.c"
#include "./expat/xmlrole.c"

#include "./gvc/gvc.c"
#include "./gvc/gvconfig.c"
#include "./gvc/gvcontext.c"
#include "./gvc/gvdevice.c"
#include "./gvc/gvevent.c"
#include "./gvc/gvjobs.c"
#include "./gvc/gvlayout.c"
#include "./gvc/gvloadimage.c"
#include "./gvc/gvplugin.c"

#undef position
#undef buffer

#include "./gvc/gvrender.c"
#include "./gvc/gvtextlayout.c"
#include "./gvc/gvtool_tred.c"
#include "./gvc/gvusershape.c"
#include "./cgraph/pend.c"
#include "./cgraph/agerror.c"
#include "./cgraph/attr.c"
#include "./cgraph/utils.c"
#include "./cgraph/graph.c"
#include "./cgraph/obj.c"
#include "./cgraph/mem.c"
#include "./cgraph/node.c"
#include "./cgraph/apply.c"
#include "./cgraph/edge.c"
#include "./cgraph/subg.c"
#include "./cgraph/refstr.c"
#include "./cgraph/rec.c"
#include "./cgraph/id.c"
#include "./cgraph/imap.c"
#include "./cgraph/io.c"
#include "./cgraph/write.c"

#undef C
#undef delta

#include "./common/arrows.c"
#include "./common/colxlate.c"
#include "./common/ellipse.c"
#include "./common/geom.c"
#include "./common/htmltable.c"
#include "./common/input.c"
#include "./common/intset.c"
#include "./common/labels.c"
#include "./common/memory.c"
#include "./common/ns.c"
#include "./common/output.c"
#include "./common/pointset.c"
#include "./common/postproc.c"
#include "./common/routespl.c"

#undef FUDGE

#include "./common/shapes.c"
#include "./common/splines.c"
#include "./common/taper.c"
#include "./common/textspan.c"
#include "./common/textspan_lut.c"
#include "./common/timing.c"

#include "./common/utils.c"
#include "./common/xml.c"
#include "./cdt/dtclose.c"
#include "./cdt/dtdisc.c"
#include "./cdt/dtextract.c"
#include "./cdt/dtflatten.c"
#include "./cdt/dthash.c"
#include "./cdt/dtlist.c"
#include "./cdt/dtmethod.c"
#include "./cdt/dtopen.c"
#include "./cdt/dtrenew.c"
#include "./cdt/dtrestore.c"
#include "./cdt/dtsize.c"
#include "./cdt/dtstat.c"
#include "./cdt/dtstrhash.c"
#include "./cdt/dttree.c"
#include "./cdt/dtview.c"
#include "./cdt/dtwalk.c"
#include "./xdot/xdot.c"
#include "./label/index.c"
#include "./label/node.c"
#include "./label/rectangle.c"
#include "./label/split.q.c"
#include "./pathplan/cvt.c"
#include "./pathplan/inpoly.c"


#include "./pathplan/route.c"
#include "./pathplan/shortestpth.c"
#include "./pathplan/solvers.c"

#undef EPS

#include "./pathplan/triang.c"
#include "./pathplan/util.c"
#include "./plugin/dot_layout/gvlayout_dot_layout.c"
#include "./plugin/dot_layout/gvplugin_dot_layout.c"
#include "./plugin/core/gvrender_core_svg.c"
#include "./dotgen/aspect.c"
#include "./dotgen/class1.c"
#include "./dotgen/class2.c"
#include "./dotgen/cluster.c"
#include "./dotgen/decomp.c"

#undef delta
#undef atts

#include "./cgraph/scan.c"

#undef multicolor
#undef left
#undef head
#undef yylex

#include "./common/emit.c"
#include "./common/psusershape.c"
#include "./common/htmllex.c"
#include "./label/xlabels.c"
#include "./pathplan/shortest.c"
#include "./pathplan/visibility.c"
#include "./plugin/core/gvplugin_core.c"
#include "./plugin/core/gvrender_core_dot.c"
#include "./dotgen/acyclic.c"
#include "./dotgen/compound.c"
#include "./dotgen/conc.c"
#include "./ortho/ortho.c"

#include "./common/htmlparse.c"

#undef YYSTACK_FREE
#undef YYNTOKENS
#undef YYFINAL
#undef YYLAST
#undef YYNNTS
#undef YYNRULES
#undef YYNSTATES
#undef YYMAXUTOK
#undef YYTABLE_NINF
#undef YYPACT_NINF
#undef yypact_value_is_default
#undef yychar
#undef yylval
#undef yynerrs
#undef yydebug
#undef yyerror
#undef yylex
#undef yyparse
#undef YYSTYPE

#include "./cgraph/grammar.c"

}

std::string convertDOTtoSVG (const std::string& DOT)
{
    using namespace GraphViz;

    std::lock_guard<decltype(lock)> l (lock);

    Agraph_t* graph = agmemread (DOT.c_str());
    auto gvc = gvContext();

    gvAddLibrary (gvc, &gvplugin_core_LTX_library);
    gvAddLibrary (gvc, &gvplugin_dot_layout_LTX_library);

    gvLayout (gvc, graph, "dot");

    char* result = nullptr;
    unsigned int length = 0;
    gvRenderData (gvc, graph, "svg", &result, &length);
    auto s = std::string (result, static_cast<size_t> (length));

    gvFreeLayout (gvc, graph);
    gvFreeRenderData (result);
    gvFreeContext (gvc);

    yylex_destroy();

    return s;
}

CHOC_REGISTER_OPEN_SOURCE_LICENCE(GraphViz, R"(
==============================================================================
Graphviz license:

Eclipse Public License - v 1.0
THE ACCOMPANYING PROGRAM IS PROVIDED UNDER THE TERMS OF THIS ECLIPSE PUBLIC LICENSE ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THE PROGRAM CONSTITUTES RECIPIENT'S ACCEPTANCE OF THIS AGREEMENT.

1. DEFINITIONS

"Contribution" means:
a) in the case of the initial Contributor, the initial code and documentation distributed under this Agreement, and
b) in the case of each subsequent Contributor:
i) changes to the Program, and
ii) additions to the Program;
where such changes and/or additions to the Program originate from and are distributed by that particular Contributor. A Contribution 'originates' from a Contributor if it was added to the Program by such Contributor itself or anyone acting on such Contributor's behalf. Contributions do not include additions to the Program which: (i) are separate modules of software distributed in conjunction with the Program under their own license agreement, and (ii) are not derivative works of the Program.
"Contributor" means any person or entity that distributes the Program.
"Licensed Patents" mean patent claims licensable by a Contributor which are necessarily infringed by the use or sale of its Contribution alone or when combined with the Program.
"Program" means the Contributions distributed in accordance with this Agreement.
"Recipient" means anyone who receives the Program under this Agreement, including all Contributors.

2. GRANT OF RIGHTS

a) Subject to the terms of this Agreement, each Contributor hereby grants Recipient a non-exclusive, worldwide, royalty-free copyright license to reproduce, prepare derivative works of, publicly display, publicly perform, distribute and sublicense the Contribution of such Contributor, if any, and such derivative works, in source code and object code form.
b) Subject to the terms of this Agreement, each Contributor hereby grants Recipient a non-exclusive, worldwide, royalty-free patent license under Licensed Patents to make, use, sell, offer to sell, import and otherwise transfer the Contribution of such Contributor, if any, in source code and object code form. This patent license shall apply to the combination of the Contribution and the Program if, at the time the Contribution is added by the Contributor, such addition of the Contribution causes such combination to be covered by the Licensed Patents. The patent license shall not apply to any other combinations which include the Contribution. No hardware per se is licensed hereunder.
c) Recipient understands that although each Contributor grants the licenses to its Contributions set forth herein, no assurances are provided by any Contributor that the Program does not infringe the patent or other intellectual property rights of any other entity. Each Contributor disclaims any liability to Recipient for claims brought by any other entity based on infringement of intellectual property rights or otherwise. As a condition to exercising the rights and licenses granted hereunder, each Recipient hereby assumes sole responsibility to secure any other intellectual property rights needed, if any. For example, if a third party patent license is required to allow Recipient to distribute the Program, it is Recipient's responsibility to acquire that license before distributing the Program.
d) Each Contributor represents that to its knowledge it has sufficient copyright rights in its Contribution, if any, to grant the copyright license set forth in this Agreement.

3. REQUIREMENTS

A Contributor may choose to distribute the Program in object code form under its own license agreement, provided that:
a) it complies with the terms and conditions of this Agreement; and
b) its license agreement:
i) effectively disclaims on behalf of all Contributors all warranties and conditions, express and implied, including warranties or conditions of title and non-infringement, and implied warranties or conditions of merchantability and fitness for a particular purpose;
ii) effectively excludes on behalf of all Contributors all liability for damages, including direct, indirect, special, incidental and consequential damages, such as lost profits;
iii) states that any provisions which differ from this Agreement are offered by that Contributor alone and not by any other party; and
iv) states that source code for the Program is available from such Contributor, and informs licensees how to obtain it in a reasonable manner on or through a medium customarily used for software exchange.

When the Program is made available in source code form:
a) it must be made available under this Agreement; and
b) a copy of this Agreement must be included with each copy of the Program.

Contributors may not remove or alter any copyright notices contained within the Program.
Each Contributor must identify itself as the originator of its Contribution, if any, in a manner that reasonably allows subsequent Recipients to identify the originator of the Contribution.

4. COMMERCIAL DISTRIBUTION

Commercial distributors of software may accept certain responsibilities with respect to end users, business partners and the like. While this license is intended to facilitate the commercial use of the Program, the Contributor who includes the Program in a commercial product offering should do so in a manner which does not create potential liability for other Contributors. Therefore, if a Contributor includes the Program in a commercial product offering, such Contributor ("Commercial Contributor") hereby agrees to defend and indemnify every other Contributor ("Indemnified Contributor") against any losses, damages and costs (collectively "Losses") arising from claims, lawsuits and other legal actions brought by a third party against the Indemnified Contributor to the extent caused by the acts or omissions of such Commercial Contributor in connection with its distribution of the Program in a commercial product offering. The obligations in this section do not apply to any claims or Losses relating to any actual or alleged intellectual property infringement. In order to qualify, an Indemnified Contributor must: a) promptly notify the Commercial Contributor in writing of such claim, and b) allow the Commercial Contributor to control, and cooperate with the Commercial Contributor in, the defense and any related settlement negotiations. The Indemnified Contributor may participate in any such claim at its own expense.
For example, a Contributor might include the Program in a commercial product offering, Product X. That Contributor is then a Commercial Contributor. If that Commercial Contributor then makes performance claims, or offers warranties related to Product X, those performance claims and warranties are such Commercial Contributor's responsibility alone. Under this section, the Commercial Contributor would have to defend claims against the other Contributors related to those performance claims and warranties, and if a court requires any other Contributor to pay any damages as a result, the Commercial Contributor must pay those damages.

5. NO WARRANTY

EXCEPT AS EXPRESSLY SET FORTH IN THIS AGREEMENT, THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is solely responsible for determining the appropriateness of using and distributing the Program and assumes all risks associated with its exercise of rights under this Agreement , including but not limited to the risks and costs of program errors, compliance with applicable laws, damage to or loss of data, programs or equipment, and unavailability or interruption of operations.

6. DISCLAIMER OF LIABILITY

EXCEPT AS EXPRESSLY SET FORTH IN THIS AGREEMENT, NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.

7. GENERAL

If any provision of this Agreement is invalid or unenforceable under applicable law, it shall not affect the validity or enforceability of the remainder of the terms of this Agreement, and without further action by the parties hereto, such provision shall be reformed to the minimum extent necessary to make such provision valid and enforceable.
If Recipient institutes patent litigation against any entity (including a cross-claim or counterclaim in a lawsuit) alleging that the Program itself (excluding combinations of the Program with other software or hardware) infringes such Recipient's patent(s), then such Recipient's rights granted under Section 2(b) shall terminate as of the date such litigation is filed.
All Recipient's rights under this Agreement shall terminate if it fails to comply with any of the material terms or conditions of this Agreement and does not cure such failure in a reasonable period of time after becoming aware of such noncompliance. If all Recipient's rights under this Agreement terminate, Recipient agrees to cease use and distribution of the Program as soon as reasonably practicable. However, Recipient's obligations under this Agreement and any licenses granted by Recipient relating to the Program shall continue and survive.
Everyone is permitted to copy and distribute copies of this Agreement, but in order to avoid inconsistency the Agreement is copyrighted and may only be modified in the following manner. The Agreement Steward reserves the right to publish new versions (including revisions) of this Agreement from time to time. No one other than the Agreement Steward has the right to modify this Agreement. The Eclipse Foundation is the initial Agreement Steward. The Eclipse Foundation may assign the responsibility to serve as the Agreement Steward to a suitable separate entity. Each new version of the Agreement will be given a distinguishing version number. The Program (including Contributions) may always be distributed subject to the version of the Agreement under which it was received. In addition, after a new version of the Agreement is published, Contributor may elect to distribute the Program (including its Contributions) under the new version. Except as expressly stated in Sections 2(a) and 2(b) above, Recipient receives no rights or licenses to the intellectual property of any Contributor under this Agreement, whether expressly, by implication, estoppel or otherwise. All rights in the Program not expressly granted under this Agreement are reserved.
This Agreement is governed by the laws of the State of New York and the intellectual property laws of the United States of America. No party to this Agreement will bring a legal action under this Agreement more than one year after the cause of action arose. Each party waives its rights to a jury trial in any resulting litigation.
)")
