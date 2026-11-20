-- DESCRIPTION: Verilator: VHDL scope an inlined subprogram body evaluates in
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package fold_pkg is
  type taps_t is array (1 to 4) of integer;
  constant table : taps_t := (1 => 0, 2 => 7, 3 => 5, 4 => 9);
  function first_over(wanted : integer) return integer;
  constant index_c : integer;  -- completed by the body, through a call
end package fold_pkg;

package body fold_pkg is
  function first_over(wanted : integer) return integer is
  begin
    for row in table'range loop
      if table(row) > wanted then
        return row;
      end if;
    end loop;
    assert false report "no entry is large enough" severity failure;
  end function first_over;

  -- The body is evaluated while the package is being elaborated, where the module
  -- scope has not been taken yet, so the loop's range comes from the package
  constant index_c : integer := first_over(4);
end package body fold_pkg;

library work;
use work.fold_pkg.all;

-- The default is evaluated while the instance's generics are bound, which is before
-- the declarative items that the module scope would otherwise hold
entity fold_gen is
  generic (g : integer := first_over(6));
  port (o_g : out integer);
end entity fold_gen;

architecture rtl of fold_gen is
begin
  o_g <= g;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;
library work;
use work.fold_pkg.all;

-- The range of the second generic's declared type calls the function, so the body is
-- evaluated while that type is resolved, with the instance's other bindings in hand
entity fold_gen2 is
  generic (
    over : positive;  -- no default, so only an instance of this entity can elaborate
    seed : std_ulogic_vector(first_over(over) downto 1) := (others => '1')
  );
  port (o_len : out integer);
end entity fold_gen2;

architecture rtl of fold_gen2 is
begin
  o_len <= seed'length;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

-- This unit does not use the package, so the names that the range's body needs can only
-- come from the context clause of the entity whose generic is being bound
entity fold_user is
  port (o_len : out integer);
end entity fold_user;

architecture rtl of fold_user is
begin
  u_gen2 : entity work.fold_gen2
    generic map (over => 8, seed => "1011")
    port map (o_len => o_len);
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;
library work;
use work.fold_pkg.all;

entity t is
  port (
    i_a : in  std_ulogic;
    o_i : out integer;
    o_g : out integer;
    o_d : out integer;
    o_len : out integer
  );
end entity t;

architecture rtl of t is
begin
  o_i <= index_c;
  o_d <= first_over(6) when i_a = '1' else first_over(8);
  u_gen : entity work.fold_gen port map (o_g => o_g);
  u_user : entity work.fold_user port map (o_len => o_len);
end architecture rtl;
