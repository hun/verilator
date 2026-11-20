-- DESCRIPTION: Verilator: an entity every instance specializes has no second top
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity child is
  generic (w : integer := 8);
  port (
    i : in  std_logic_vector(w - 1 downto 0);
    o : out std_logic_vector(w - 1 downto 0)
  );
end entity child;

architecture rtl of child is
begin
  o <= i;
end architecture rtl;

entity tb is
end entity tb;

architecture sim of tb is
  signal i : std_logic_vector(7 downto 0) := "10110010";
  signal o : std_logic_vector(7 downto 0);
begin
  -- Both instances bind the generic, so the child is elaborated twice and its base module
  -- is driven by nothing: without the fix it is a second top and the run refuses to start
  u_lo : entity work.child
    generic map (w => 4)
    port map (i => i(3 downto 0), o => o(3 downto 0));

  u_hi : entity work.child
    generic map (w => 4)
    port map (i => i(7 downto 4), o => o(7 downto 4));

  check : process
  begin
    wait for 1 ns;
    if o = "10110010" then
      report "both specializations drive their ports";
    else
      report "specializations mismatch";
    end if;
    std.env.finish;
    wait;
  end process check;
end architecture sim;