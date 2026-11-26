-- DESCRIPTION: Verilator: constants of a package body, and non-integer constants built
-- by calling functions another package defines in its body
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- PoC's configuration package keeps its strings, board tables and tool name in the body of
-- the package, built by functions that also live in that body, and other packages take
-- their values from those functions. A constant of the body is therefore visible inside
-- the bodies of the same package, and a specification constant whose initializer calls
-- such a function has to wait for the pass that reads the body.

package pkg_a is
  function tag(i : integer) return string;
  function bits(i : integer) return std_logic_vector;
  function version return integer;
  function scaled(x : integer := 2) return integer;
end package;

package body pkg_a is
  constant mark_c : character := '!';

  function tag(i : integer) return string is
    variable s : string(1 to 2);
  begin
    s(1) := '3';
    s(2) := mark_c;
    if i /= 3 then
      s(1) := 'x';
    end if;
    return s;
  end function;

  function bits(i : integer) return std_logic_vector is
  begin
    if i = 3 then
      return "0011";
    end if;
    return "0000";
  end function;

  function version return integer is
  begin
    return 5;
  end function;

  function scaled(x : integer := 2) return integer is
  begin
    return x * 5;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;

use work.pkg_a.all;

package pkg_b is
  constant tag_c  : string := pkg_a.tag(3);
  constant bits_c : std_logic_vector(3 downto 0) := pkg_a.bits(3);
  -- A function without arguments, and one whose only formal has a default, are called by
  -- naming them alone (IEEE 1076-2008 9.3.4)
  constant ver_c   : integer := version;
  constant scale_c : integer := scaled;
end package;

library ieee;
use ieee.std_logic_1164.all;

use work.pkg_b.all;

entity t is
  port (
    clk : in  std_logic;
    o   : out std_logic
  );
end entity;

architecture rtl of t is
  signal ok : std_logic;
begin
  ok <= '1' when tag_c = "3!" and bits_c = "0011" and ver_c = 5 and scale_c = 10 else '0';

  process (clk)
  begin
    if rising_edge(clk) then
      o <= ok;
    end if;
  end process;
end architecture;
