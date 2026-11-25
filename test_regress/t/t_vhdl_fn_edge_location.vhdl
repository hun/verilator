-- DESCRIPTION: Verilator: nested loops with a run-time actual and an element write
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- microwatt's edgelocation: both loop bounds fold, but the actual is a signal, which the
-- expression form would copy into each of the reads of v

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is end entity;

architecture rtl of t is
  function edgelocation(v : std_ulogic_vector; nbits : natural) return std_ulogic_vector is
    variable p : std_ulogic_vector(nbits - 1 downto 0);
    variable stride : natural;
    variable b : std_ulogic;
    variable k : natural;
  begin
    stride := 2;
    for i in 0 to nbits - 1 loop
      b := '0';
      for j in 0 to (2 ** nbits / stride) - 1 loop
        k := j * stride;
        b := b or (v(k + stride - 1) and not v(k + (stride / 2) - 1));
      end loop;
      p(i) := b;
      stride := stride * 2;
    end loop;
    return p;
  end function;
  signal edge : std_ulogic_vector(63 downto 0) := (others => '1');
  signal loc : std_ulogic_vector(5 downto 0);
begin
  loc <= edgelocation(edge, 6);
  process
    procedure check(k : natural) is
    begin
      edge <= std_ulogic_vector(shift_left(unsigned'(x"FFFFFFFFFFFFFFFF"), k));
      wait for 1 ns;
      assert loc = std_ulogic_vector(to_unsigned(k, 6)) report "edge at " & integer'image(k)
        severity failure;
    end procedure;
  begin
    check(1);
    check(5);
    check(37);
    check(63);
    std.env.finish;
    wait;
  end process;
end architecture;
