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
package com.db4o.reflect.generic;

public class GenericObjectBase {

	private final GenericClass _class;
	private Object[] _values;

	public GenericObjectBase(GenericClass clazz) {
		_class = clazz;
	}

	private void ensureValuesInitialized() {
		if(_values == null) {
			_values = new Object[_class.getFieldCount()];
		}
	}

	public void set(int index, Object value) {
		ensureValuesInitialized();
		_values[index]=value;
	}

	/**
	 *
	 * @param index
	 * @return the value of the field at index, based on the fields obtained GenericClass.getDeclaredFields
	 */
	public Object get(int index) {
		ensureValuesInitialized();
		return _values[index];
	}

	public String toString() {
	    if(_class == null){
	        return super.toString();    
	    }
	    return _class.toString(cast());
	}

	private GenericObject cast() {
		return (GenericObject)this;
	}

	public GenericClass getGenericClass() {
		return _class;
	}

}