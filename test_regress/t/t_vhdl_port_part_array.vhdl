-- DESCRIPTION: Verilator: VHDL ports of an array of vectors associated by element and by slice
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
package pk is
  type byte is array (0 to 1) of bit_vector(3 downto 0);
  type rev is array (3 downto 0) of bit_vector(1 downto 0);
end package pk;

use work.pk.all;

entity sub is
  port (p : in byte; r : in rev; y0, y1 : out bit_vector(3 downto 0);
        z : out bit_vector(7 downto 0); q : out byte);
end entity sub;

architecture rtl of sub is
begin
  y0 <= p(0);
  y1 <= p(1);
  z <= r(3) & r(2) & r(1) & r(0);
  q <= p;
end architecture rtl;

use work.pk.all;

entity t is
end entity t;

architecture sim of t is
  signal a : bit_vector(3 downto 0) := "1010";
  signal c : bit_vector(3 downto 0) := "0110";
  signal e3 : bit_vector(1 downto 0) := "11";
  signal e2 : bit_vector(1 downto 0) := "01";
  signal e1 : bit_vector(1 downto 0) := "00";
  signal e0 : bit_vector(1 downto 0) := "11";
  signal y0, y1 : bit_vector(3 downto 0);
  signal z : bit_vector(7 downto 0);
  signal o0, o1 : bit_vector(3 downto 0);
  signal both : byte;
  signal ab : byte := ("1001", "0100");
  signal q : byte;
begin
  -- Each part of p is a whole element, and q is split into its two elements
  u : entity work.sub
    port map (p(0) => a, p(1) => c, r(3) => e3, r(2) => e2, r(1) => e1, r(0) => e0,
              y0 => y0, y1 => y1, z => z, q(0) => o0, q(1) => o1);

  -- A slice of elements takes a whole array
  v : entity work.sub
    port map (p(0 to 1) => ab, r(3) => e1, r(2) => e1, r(1) => e1, r(0) => e1, q(0 to 1) => q);

  check : process
  begin
    wait for 1 ns;
    assert y0 = "1010" severity failure;
    assert y1 = "0110" severity failure;
    assert z = "11010011" severity failure;
    assert o0 = "1010" severity failure;
    assert o1 = "0110" severity failure;
    assert q(0) = "1001" severity failure;
    assert q(1) = "0100" severity failure;
    report "array part ports done";
    wait;
  end process check;
end architecture sim;
