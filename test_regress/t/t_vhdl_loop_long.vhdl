-- DESCRIPTION: Verilator: a loop past the unroll limit, run as a loop
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Each range is longer than the unroll limit: ascending, descending, and one left by exit

entity t is end entity;
architecture a of t is
  signal cu, cd, ex : integer := 0;
  signal done : bit := '0';
begin
  process begin
    wait for 1 ns;
    for i in 0 to 9999 loop cu <= i; wait for 5 ns; end loop;
    for i in 9999 downto 0 loop cd <= i; wait for 5 ns; end loop;
    for i in 0 to 9999 loop
      if i = 7000 then exit; end if;
      ex <= i; wait for 1 ns;
    end loop;
    done <= '1'; wait;
  end process;
  process begin
    wait until done = '1';
    assert cu = 9999 report "up" severity failure;
    assert cd = 0 report "down" severity failure;
    assert ex = 6999 report "exit" severity failure;
    report "PASS";
    std.env.finish;
  end process;
end architecture;
