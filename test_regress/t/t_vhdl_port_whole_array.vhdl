-- DESCRIPTION: Verilator: VHDL array-of-vector ports associated as a whole
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
package pk is
  type byte is array (0 to 1) of bit_vector(3 downto 0);
end package pk;

use work.pk.all;

entity sub is
  port (p : in byte; q : out byte);
end entity sub;

architecture rtl of sub is
begin
  q <= p;
end architecture rtl;

use work.pk.all;

entity t is
end entity t;

architecture sim of t is
  signal a : byte := ("1010", "0110");
  signal o : byte;
  signal o2 : byte;
begin
  u : entity work.sub port map (p => a, q => o);
  v : entity work.sub port map (p => ("0001", "1000"), q => o2);

  check : process
  begin
    wait for 1 ns;
    assert o(0) = "1010" severity failure;
    assert o(1) = "0110" severity failure;
    assert o2(0) = "0001" severity failure;
    assert o2(1) = "1000" severity failure;
    report "whole array ports done";
    wait;
  end process check;
end architecture sim;
