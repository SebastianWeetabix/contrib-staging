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
package com.db4o.internal.cs;

import com.db4o.foundation.*;
import com.db4o.internal.*;
import com.db4o.internal.cs.messages.*;

/**
 * @exclude
 */
public class ClientHeartbeat implements Runnable {
    
    private SimpleTimer _timer; 
    
    private final ClientObjectContainer _container;

    public ClientHeartbeat(ClientObjectContainer container) {
        _container = container;
        _timer = new SimpleTimer(this, frequency(container.configImpl()), "db4o client heartbeat");
    }
    
    private int frequency(Config4Impl config){
        return Math.min(config.timeoutClientSocket(), config.timeoutServerSocket()) / 2;
    }

    public void run() {
        _container.writeMessageToSocket(Msg.PING);
    }
    
    public void start(){
        _timer.start();
    }

    public void stop() {
        if (_timer == null){
            return;
        }
        _timer.stop();
        _timer = null;
    }

}
