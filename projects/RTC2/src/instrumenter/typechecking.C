#include "instr.h"


namespace TypeCheck
{

  int getClassification(SgType* ptr)
  {
    if(isSgArrayType(ptr))
      return 1;
    else if(SI::isStructType(ptr))
      return 2;
    else if(isSgClassType(ptr)) //non-struct class type == union.
      return 3;
    else if(SI::isPointerType(ptr))
      return 4;
    else
      return 0; //Primitive
  }

  bool contains(std::vector<std::string> v, std::string x)
  {
    return (std::find(v.begin(), v.end(), x) != v.end())
  }

//Check to see if instrumentation is needed in the case of the given type
  bool shouldInstrumentType(SgType* ptr)
  {
    bool res = !( isSgFunctionType(ptr) || isSgFunctionType(ptr->findBaseType())
                  || isSgTypeEllipse(ptr)
                  || (isSgArrayType(ptr) && isSgNullExpression(isSgArrayType(ptr)->get_index())) //incomplete array type (e.g.
                  || (isSgNamedType(ptr) && isSgNamedType(ptr)->get_declaration()->get_definingDeclaration() == NULL ) //incomplete struct/union type (no defining declaration);
                );
    return res;


  }

  void instr(SgProject* project, Rose_STL_Container<SgType*> types)
  {
    SgGlobal* global = SI::getFirstGlobalScope(project);
    std::string prefix("rtc_ti_"); //struct prefix
    std::string ti_type_str("struct rtc_typeinfo*");
    SgType* ti_type = SB::buildOpaqueType(ti_type_str,global);

    //Insert declarations from the typechecking library.

    //void minalloc_check(unsigned long long addr)
    SgFunctionDeclaration* minalloc_check_decl = SB::buildNondefiningFunctionDeclaration(
          SgName("minalloc_check"),
          SgTypeVoid::createType(),
          SB::buildFunctionParameterList(
            SB::buildInitializedName("addr",SB::buildUnsignedLongLongType())),
          global,NULL);
    SI::prependStatement(minalloc_check_decl,global);

    //void typetracker_add(unsigned long long addr, struct rtc_typeinfo* ti);
    SgFunctionDeclaration* typetracker_add_decl = SB::buildNondefiningFunctionDeclaration(
          SgName("typetracker_add"),
          SgTypeVoid::createType(),
          SB::buildFunctionParameterList(
            SB::buildInitializedName("addr",SB::buildUnsignedLongLongType()),
            SB::buildInitializedName("ti",ti_type)),
          global,NULL);
    SI::prependStatement(typetracker_add_decl,global);

    //void setBaseType(rtc_typeinfo* ti, rtc_typeinfo* base)
    SgFunctionDeclaration* setBaseType_decl = SB::buildNondefiningFunctionDeclaration(
          SgName("setBaseType"),
          SgTypeVoid::createType(),
          SB::buildFunctionParameterList(
            SB::buildInitializedName("ti",ti_type),
            SB::buildInitializedName("base",ti_type)),
          global,NULL);
    SI::prependStatement(setBaseType_decl,global);

    //struct rtc_typeinfo* ti_init(const char* a, size_t sz, int c)
    SgFunctionDeclaration* ti_init_decl = SB::buildNondefiningFunctionDeclaration(
                                            SgName("ti_init"),
                                            ti_type,
                                            SB::buildFunctionParameterList(
//    SB::buildInitializedName("a",SB::buildPointerType(SB::buildConstType(SB::buildCharType()))),
                                                SB::buildInitializedName("a",SB::buildPointerType(SB::buildCharType())),
//    SB::buildInitializedName("sz", SB::buildOpaqueType("size_t",global)),
                                                SB::buildInitializedName("sz", SB::buildLongLongType()),
                                                SB::buildInitializedName("c", SB::buildIntType())),
                                            global,NULL);
    SI::prependStatement(ti_init_decl,global);

    //void traverseAndPrint()
    SgFunctionDeclaration* traverseAndPrint_decl = SB::buildNondefiningFunctionDeclaration(
          SgName("traverseAndPrint"),SgTypeVoid::createType(),SB::buildFunctionParameterList(),global,NULL);
    SI::prependStatement(traverseAndPrint_decl,global);

    //non-defining declaration of rtc_init_typeinfo
    SgName init_name("rtc_init_typeinfo");
    SgFunctionDeclaration* init_nondef = SB::buildNondefiningFunctionDeclaration(init_name,SgTypeVoid::createType(),SB::buildFunctionParameterList(),global,NULL);
    SI::prependStatement(init_nondef,global);

    //call to rtc_init_typeinfo placed in main function.
    SgFunctionDeclaration* maindecl = SI::findMain(project);
    SgExprStatement* initcall = SB::buildFunctionCallStmt(init_name,SgTypeVoid::createType(),NULL,maindecl->get_definition());
    maindecl->get_definition()->prepend_statement(initcall);

    //defining declaration of rtc_init_typeinfo
    SgFunctionDeclaration* init_definingDecl = new SgFunctionDeclaration(new Sg_File_Info(SI::getEnclosingFileNode(global)->getFileName()),init_name,init_nondef->get_type(),NULL);
    init_definingDecl->set_firstNondefiningDeclaration(init_nondef);
    SgFunctionDefinition* init_definition = new SgFunctionDefinition(new Sg_File_Info(SI::getEnclosingFileNode(global)->getFileName()),init_definingDecl,SB::buildBasicBlock());
    init_definingDecl->set_definition(init_definition);
    SI::appendStatement(init_definingDecl,global);

    std::vector<std::string> lst;
    for(unsigned int index = 0; index < types.size(); index++)
    {
      SgType* ptr = types[index];

      ptr = ptr->stripTypedefsAndModifiers();
      if(!shouldInstrumentType(ptr))
        continue;
      std::string nameStr = prefix + Util::getNameForType(ptr).getString();
      if(!contains(lst,nameStr))
      {
        SgVariableDeclaration* decl = SB::buildVariableDeclaration(nameStr,ti_type,NULL,global);
        SI::prependStatement(decl,global);
        lst.push_back(nameStr);
      }
    }

    for(unsigned int index = 0; index < types.size(); index++)
    {
      SgType* ptr = types[index];
      ptr = ptr->stripTypedefsAndModifiers();
      if(!shouldInstrumentType(ptr))
        continue;
      std::string typeNameStr = Util::getNameForType(ptr).getString();
      std::string structNameStr = prefix + Util::getNameForType(ptr).getString();

      if(contains(lst,structNameStr))
      {
        SgExpression* lhs;
        SgExpression* rhs;

        //In case of an anonymous struct or union, we create a local, named version of the declaration so we can know its size.
        SgClassDeclaration* altDecl = NULL;
        if(isSgNamedType(ptr) && isSgClassDeclaration(isSgNamedType(ptr)->get_declaration()) && isSgClassDeclaration(isSgNamedType(ptr)->get_declaration())->get_isUnNamed())
        {
          SgClassDeclaration* originalDecl = isSgClassDeclaration(isSgNamedType(ptr)->get_declaration()->get_definingDeclaration());
          SgName altDecl_name(typeNameStr + "_def");
          altDecl = new SgClassDeclaration(new Sg_File_Info(SI::getEnclosingFileNode(global)->getFileName()),altDecl_name,originalDecl->get_class_type());

          SgClassDefinition* altDecl_definition = SB::buildClassDefinition(altDecl);

          SgDeclarationStatementPtrList originalMembers = originalDecl->get_definition()->get_members();
          for(SgDeclarationStatementPtrList::iterator it = originalMembers.begin(); it != originalMembers.end(); it++)
          {
            SgDeclarationStatement* member = *it;
            SgDeclarationStatement* membercpy = isSgDeclarationStatement(SI::copyStatement(member));
            altDecl_definition->append_member(membercpy);
          }


          SgClassDeclaration* altDecl_nondef = new SgClassDeclaration(new Sg_File_Info(SI::getEnclosingFileNode(global)->getFileName()),altDecl_name,originalDecl->get_class_type());

          altDecl_nondef->set_scope(global);
          altDecl->set_scope(global);

          altDecl->set_firstNondefiningDeclaration(altDecl_nondef);
          altDecl_nondef->set_firstNondefiningDeclaration(altDecl_nondef);
          altDecl->set_definingDeclaration(altDecl);
          altDecl_nondef->set_definingDeclaration(altDecl);


          SgClassType* altDecl_ct = SgClassType::createType(altDecl_nondef);
          altDecl->set_type(altDecl_ct);
          altDecl_nondef->set_type(altDecl_ct);

          altDecl->set_isUnNamed(false);
          altDecl_nondef->set_isUnNamed(false);

          altDecl_nondef->set_forward(true);

          SgSymbol* sym = new SgClassSymbol(altDecl_nondef);
          global->insert_symbol(altDecl_name, sym);

          altDecl->set_linkage("C");
          altDecl_nondef->set_linkage("C");

          ROSE_ASSERT(sym && sym->get_symbol_basis() == altDecl_nondef);
          ROSE_ASSERT(altDecl->get_definingDeclaration() == altDecl);
          ROSE_ASSERT(altDecl->get_firstNondefiningDeclaration() == altDecl_nondef);
          ROSE_ASSERT(altDecl->search_for_symbol_from_symbol_table() == sym);
          ROSE_ASSERT(altDecl->get_definition() == altDecl_definition);
          ROSE_ASSERT(altDecl->get_scope() == global && altDecl->get_scope() == altDecl_nondef->get_scope());
          ROSE_ASSERT(altDecl_ct->get_declaration() == altDecl_nondef);

          //For some reason, this is not working...

          //global->append_statement(altDecl);
          //global->prepend_statement(altDecl_nondef);

          //SI::setOneSourcePositionForTransformation(altDecl);
          //SI::setOneSourcePositionForTransformation(altDecl_nondef);
        }

        SgType* baseType;
        if(isSgPointerType(ptr))
          baseType = ptr->dereference();
        else
          baseType = ptr->findBaseType();

        baseType = baseType->stripTypedefsAndModifiers();
        if(baseType == NULL || baseType == ptr)
        {
          //In this case, there is no base type.
          rhs = SB::buildFunctionCallExp(SgName("ti_init"),SgTypeVoid::createType(),SB::buildExprListExp(
                                           SB::buildStringVal(ptr->unparseToString()),
                                           ((altDecl == NULL && !isSgTypeVoid(ptr)) ? (SgExpression*) SB::buildSizeOfOp(types[index]) : (SgExpression*) SB::buildIntVal(-1)),
                                           SB::buildIntVal(getClassification(ptr))
                                         ),init_definition);
        }
        else
        {
          //The type has a base type.
          std::string baseStructNameStr = prefix + Util::getNameForType(baseType).getString();
          rhs = SB::buildFunctionCallExp(SgName("ti_init"),ti_type,SB::buildExprListExp(
                                           SB::buildStringVal(ptr->unparseToString()),
                                           ((altDecl == NULL && !isSgTypeVoid(ptr)) ? (SgExpression*) SB::buildSizeOfOp(types[index]) : (SgExpression*) SB::buildIntVal(-1)),
                                           SB::buildIntVal(getClassification(ptr))
                                         ),init_definition);

          SgExprStatement* set_BT = SB::buildFunctionCallStmt(SgName("setBaseType"),ti_type,SB::buildExprListExp(
                                      SB::buildVarRefExp(structNameStr),
                                      SB::buildVarRefExp(baseStructNameStr)),
                                    init_definition);
          init_definition->append_statement(set_BT);
        }
        lhs = SB::buildVarRefExp(structNameStr);


        SgExprStatement* assignstmt = SB::buildAssignStatement(lhs,rhs);
        init_definition->prepend_statement(assignstmt);
        std::remove(lst.begin(),lst.end(),structNameStr);

      }



    }
  }



};
