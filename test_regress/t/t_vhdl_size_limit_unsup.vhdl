-- DESCRIPTION: Verilator: a case choice range or an array too large for the model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is end entity;
architecture a of t is
  signal m : integer := 7;
  signal k : integer := 0;
begin
  p : process (m)
    variable big : string(1 to 50331648);
  begin
    null;
  end process;
end architecture;
