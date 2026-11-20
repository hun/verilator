-- DESCRIPTION: Verilator: a declaration hides an imported one of the same profile
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A unit's own declaration hides an imported homograph (IEEE 1076-2008 12.4), so the
-- package that `use work.base.all` made visible never answers this call. The second
-- output inlines a specification function whose body calls a declaration only the body
-- makes: the inlined body keeps the package's private scope, where that declaration
-- hides the imported one again.

library ieee;
use ieee.std_logic_1164.all;

package base is
  function tag(i : integer) return integer;
end package;

package body base is
  function tag(i : integer) return integer is
  begin
    return 1;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.base.all;

package child is
  function tag(i : integer) return integer;
  function pick return integer;
end package;

package body child is
  -- Private to the body: base.tag is imported here as well, and this declaration wins
  function double_it(i : integer) return integer is
  begin
    return 2 * i;
  end function;

  function tag(i : integer) return integer is
  begin
    return double_it(1);
  end function;

  function pick return integer is
  begin
    return tag(0);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.child.all;

entity t is
  port (
    clk    : in  std_logic;
    o_spec : out std_logic_vector(7 downto 0);
    o_body : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of t is
  -- child's specification declares tag, which hides base.tag here
  constant spec_c : integer := tag(0);
begin
  process (clk) begin
    if rising_edge(clk) then
      o_spec <= std_logic_vector(to_unsigned(spec_c, 8));
      -- pick's body calls the body's own tag, which must not report an ambiguity
      o_body <= std_logic_vector(to_unsigned(pick, 8));
    end if;
  end process;
end architecture;
