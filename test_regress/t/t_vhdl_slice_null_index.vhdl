-- DESCRIPTION: Verilator: a null slice of a defaulted generic, converted to integer
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The entity sub is elaborated with its default W, where a(W + 1 downto 2) is the null
-- range a(1 downto 2). numeric_std gives 0 for a null argument (IEEE 1076.3 TO_INTEGER),
-- so the default elaboration must not fail. The instance binds W, which selects a(5 downto 2).

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sub is
  generic (
    W : natural := 0
  );
  port (
    a : in  std_logic_vector(7 downto 0);
    o : out integer
  );
end entity;

architecture rtl of sub is
begin
  o <= to_integer(unsigned(a(W + 1 downto 2)));
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity t is
end entity t;

architecture sim of t is
  signal a : std_logic_vector(7 downto 0) := x"34";
  signal o : integer;
begin
  u_sub : entity work.sub
    generic map (W => 4)
    port map (a => a, o => o);

  check : process
  begin
    wait for 1 ns;
    assert o = 13 report "o is " & integer'image(o) severity failure;
    std.env.finish;
    wait;
  end process check;
end architecture sim;
