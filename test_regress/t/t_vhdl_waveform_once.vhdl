-- DESCRIPTION: Verilator: VHDL delayed concurrent assignments of constants run once
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
entity t is
end entity t;

architecture sim of t is
  signal o : bit := '0';
  signal w : bit := '0';
  signal p : bit := '0';
begin
  -- No signal is read, so each assignment runs once and the simulation ends by itself
  o <= '1' after 5 ns, '0' after 10 ns;
  w <= '0', '1' after 2 ns, '0' after 6 ns;
  p <= '1' after 3 ns;

  check : process
  begin
    wait for 1 ns;
    assert o = '0' severity failure;
    assert w = '0' severity failure;
    assert p = '0' severity failure;
    wait for 3 ns;
    assert o = '0' severity failure;
    assert w = '1' severity failure;
    assert p = '1' severity failure;
    wait for 3 ns;
    assert o = '1' severity failure;
    assert w = '0' severity failure;
    wait for 4 ns;
    assert o = '0' severity failure;
    assert p = '1' severity failure;
    report "waveforms done";
    wait;
  end process check;
end architecture sim;
