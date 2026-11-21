-- DESCRIPTION: Verilator: VHDL record constraints naming a range per member
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A record whose elements are unconstrained can be constrained where it is used:
-- `t_rec(a(3 downto 0), b(7 downto 0))`. A member's range may also be named by an
-- attribute of another object, `t_rec(a(x.a'range), b(x.b'range))`, which is how
-- poc's AddressResize gives a record a new address width. `open` leaves the index of
-- an array to the object that names the subtype, so only the record element is
-- constrained. Every value below is the one GHDL and nvc report.

library ieee;
use ieee.std_logic_1164.all;

package recpkg is
  type t_rec is record
    a : std_logic_vector;
    b : std_logic_vector;
  end record;
  type t_arr is array (natural range <>) of t_rec;
  subtype t_arr4 is t_arr(open)(a(3 downto 0), b(7 downto 0));
  function widen (x : t_rec) return t_rec;
end package;

package body recpkg is
  function widen (x : t_rec) return t_rec is
    -- Each member takes its range from the actual, as an unconstrained record must
    variable r : t_rec(a(x.a'range), b(x.b'range));
  begin
    r := x;
    return r;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.recpkg.all;

entity t_vhdl_record_constraint is
  port (
    o : out std_logic_vector(3 downto 0);
    p : out std_logic_vector(7 downto 0);
    w : out std_logic_vector(3 downto 0);
    n : out integer
  );
end entity;

architecture rtl of t_vhdl_record_constraint is
  signal s : t_arr4(0 to 1);
  signal v : t_rec(a(3 downto 0), b(7 downto 0));
begin
  s(0).a <= "0011";
  s(0).b <= x"5a";
  s(1).a <= "1100";
  s(1).b <= x"a5";
  v <= widen(s(0));
  o <= s(1).a;
  p <= s(0).b;
  w <= v.a;
  n <= s'length;
end architecture;
