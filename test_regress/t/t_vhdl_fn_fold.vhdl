-- DESCRIPTION: Verilator: VHDL statements of a function body evaluated at compile time
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A call whose result is static is evaluated while lowering, so a function body has to run
-- its statements and not only its last return: first_length walks a table of rows and
-- returns from inside nested loops, and ends in an assertion rather than in a value it
-- cannot name, as hdl-modules' lfsr_pkg does; scaled picks the case arm its argument
-- already decides, without an 'others' to fall back on. dyn_max is the same walk with an
-- argument only known at run time, where every path still has to yield a value. The table's
-- row type is a subtype of an imported package's unconstrained array, as lfsr_pkg declares
-- it.

package types_pkg is
  type natural_vec_t is array (natural range <>) of integer;
end package types_pkg;

package p is
  use work.types_pkg.all;

  subtype taps_t is natural_vec_t(0 to 2);
  type table_t is array (1 to 4) of taps_t;
  constant table : table_t := (
    1 => (0, 0, 0),
    2 => (1, 0, 7),
    3 => (2, 5, 0),
    4 => (3, 9, 0)
  );

  function first_length(wanted : integer; minimum : integer) return integer;
  function scaled(value : integer range 2 to 3) return integer;
  function dyn_max(wanted : integer) return integer;
end package p;

package body p is
  function first_length(wanted : integer; minimum : integer) return integer is
    variable found : boolean := false;
  begin
    for row in table'range loop
      found := true;
      for col in table(row)'range loop
        if table(row)(col) /= 0 then
          if table(row)(col) > wanted then
            found := false;
          end if;
        end if;
      end loop;
      if row < minimum then
        found := false;
      end if;
      if found then
        return row;
      end if;
    end loop;
    assert false report "no row is long enough" severity failure;
  end function first_length;

  function scaled(value : integer range 2 to 3) return integer is
    variable result : integer := 0;
  begin
    case value is
      when 2 => result := 20;
      when 3 => result := 30;
    end case;
    return result;
  end function scaled;

  function dyn_max(wanted : integer) return integer is
    variable best : integer := 0;
  begin
    for row in table'range loop
      if table(row)(0) > best and table(row)(0) <= wanted then
        best := table(row)(0);
      end if;
    end loop;
    return best;
  end function dyn_max;
end package body p;

use work.p.all;

entity t is
  port (
    i_a       : in  integer;
    o_first1  : out integer;
    o_first2  : out integer;
    o_scaled3 : out integer;
    o_scaled2 : out integer;
    o_dyn     : out integer
  );
end entity t;

architecture rtl of t is
  constant first1  : integer := first_length(5, 2);
  constant first2  : integer := first_length(10, 2);
  constant scaled3 : integer := scaled(3);
  constant scaled2 : integer := scaled(2);
begin
  o_first1  <= first1;
  o_first2  <= first2;
  o_scaled3 <= scaled3;
  o_scaled2 <= scaled2;
  o_dyn     <= dyn_max(i_a);
end architecture rtl;
