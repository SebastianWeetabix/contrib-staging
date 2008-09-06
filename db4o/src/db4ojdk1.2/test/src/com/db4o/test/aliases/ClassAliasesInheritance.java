/* Copyright (C) 2004 - 2008  db4objects Inc.  http://www.db4o.com

This file is part of the db4o open source object database.

db4o is free software; you can redistribute it and/or modify it under
the terms of version 2 of the GNU General Public License as published
by the Free Software Foundation and as clarified by db4objects' GPL 
interpretation policy, available at
http://www.db4o.com/about/company/legalpolicies/gplinterpretation/
Alternatively you can write to db4objects, Inc., 1900 S Norfolk Street,
Suite 350, San Mateo, CA 94403, USA.

db4o is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. */
package com.db4o.test.aliases;

import com.db4o.*;
import com.db4o.config.*;
import com.db4o.test.*;



public class ClassAliasesInheritance {
    
    public void test(){
        
        Test.store(new Parent1(new Child1()));
        
        ObjectContainer container = Test.reOpen();
        container.ext().configure().addAlias(
            new TypeAlias("com.db4o.test.aliases.Parent1",
                        "com.db4o.test.aliases.Parent2"));
        container.ext().configure().addAlias(
            new TypeAlias("com.db4o.test.aliases.Child1",
                        "com.db4o.test.aliases.Child2"));
        
        ObjectSet os = container.query(Parent2.class);
        
        Test.ensure(os.size() > 0);
        
        Parent2 p2 = (Parent2)os.next();
        
        Test.ensure(p2.child != null);
    }

}
